#!/usr/bin/env python3
"""Offline converter: turns every Dimensional Sinking Depot in a Satisfactory save (.sav) or blueprint (.sbp) into a
vanilla Dimensional Depot Uploader, without the game or the mod.

Use it when the mod can't be loaded (for example right after a game update), or to clean blueprint files. With the mod
working, the in-game command (/sinkingdepot convert, or SinkingDepot.ConvertAll on a dedicated server) does the same
for a running world.

What changes for each Sinking Depot:
  - It becomes a vanilla uploader in the same place, with the same belt, buffered items and paint.
  - Its built-with recipe becomes the vanilla uploader's, so dismantling refunds the vanilla cost.
  - Its power connector is removed, together with any power wires attached to it (vanilla uploaders take no power).
    Nothing is refunded for them.
  - The mod's own bookkeeping object (the undo record of the in-game command) is removed.
In a blueprint, the recipe list and the cost in the blueprint's header are updated to match.

The input file is never changed: the result is written next to it as <name>_vanilla.sav / .sbp (or into --out-dir).
Standard library only (Python 3.9 or newer). Tested with saves and blueprints from game version 1.2.5.1 (save
version 60): converted, then loaded in the game without the mod. Saves from 1.2.0 (save version 58) are read back
exactly too. Files from other versions are refused unless you pass --force.

Usage:
  python convert_to_vanilla.py <file or folder> [more files or folders] [--out-dir DIR] [--force] [--dry-run]
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

MOD_PACKAGE = '/DimensionalSinkingDepot/'
OUR_BUILD = '/DimensionalSinkingDepot/Buildable/Build_SinkingDepot.Build_SinkingDepot_C'
OUR_RECIPE = '/DimensionalSinkingDepot/Buildable/Recipe_SinkingDepot.Recipe_SinkingDepot_C'
OUR_SUBSYSTEM = '/Script/DimensionalSinkingDepot.SinkingDepotSubsystem'
OUR_CONNECTOR_NAME = 'PowerConnection'
VANILLA_BUILD = '/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C'
VANILLA_RECIPE = '/Game/FactoryGame/Recipes/Buildings/Recipe_CentralStorage.Recipe_CentralStorage_C'
POWER_LINE = '/Game/FactoryGame/Buildable/Factory/PowerLine/Build_PowerLine.Build_PowerLine_C'

# What a Sinking Depot costs on top of a vanilla uploader (the AWESOME Sink half), for blueprint headers.
SINK_HALF_COST = {
    '/Game/FactoryGame/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced.Desc_IronPlateReinforced_C': 15,
    '/Game/FactoryGame/Resource/Parts/Cable/Desc_Cable.Desc_Cable_C': 30,
    '/Game/FactoryGame/Resource/Parts/Cement/Desc_Cement.Desc_Cement_C': 45,
}

CHUNK_TAG = 0x9E2A83C1
TESTED_SAVE_VERSIONS = {58, 60}  # 58: game 1.2.0 (read back exactly), 60: game 1.2.5.1 (converted and loaded in game)
TESTED_HEADER_VERSIONS = {14}
TESTED_BLUEPRINT_HEADER_VERSIONS = {2}


class FormatError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------------
# Binary helpers. Strings that are not edited keep their original bytes, so untouched data round-trips exactly.

class Reader:
    def __init__(self, data: bytes, pos: int = 0, end: int | None = None):
        self.b = data
        self.p = pos
        self.end = len(data) if end is None else end

    def need(self, n: int):
        if n < 0 or self.p + n > self.end:
            raise FormatError(f'unexpected end of data at offset {self.p} (wanted {n} bytes)')

    def raw(self, n: int) -> bytes:
        self.need(n)
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def u8(self) -> int:
        self.need(1)
        v = self.b[self.p]
        self.p += 1
        return v

    def i32(self) -> int:
        self.need(4)
        v = struct.unpack_from('<i', self.b, self.p)[0]
        self.p += 4
        return v

    def i64(self) -> int:
        self.need(8)
        v = struct.unpack_from('<q', self.b, self.p)[0]
        self.p += 8
        return v

    def fstr_raw(self) -> tuple[str, bytes]:
        """Reads an FString; returns (text, the exact bytes it occupied)."""
        start = self.p
        n = self.i32()
        if n == 0:
            return '', self.b[start:self.p]
        if n > 0:
            if n > 1 << 24:
                raise FormatError(f'implausible string length {n} at offset {start}')
            text = self.raw(n)[:-1].decode('latin-1')
        else:
            n = -n
            if n > 1 << 24:
                raise FormatError(f'implausible string length {n} at offset {start}')
            text = self.raw(2 * n)[:-2].decode('utf-16-le')
        return text, self.b[start:self.p]

    def fstr(self) -> str:
        return self.fstr_raw()[0]


def fstr_bytes(text: str) -> bytes:
    if text == '':
        return struct.pack('<i', 0)
    try:
        data = text.encode('ascii') + b'\x00'
        return struct.pack('<i', len(data)) + data
    except UnicodeEncodeError:
        data = text.encode('utf-16-le') + b'\x00\x00'
        return struct.pack('<i', -(len(data) // 2)) + data


# ---------------------------------------------------------------------------------------------------------------------
# Compressed file container (shared by .sav and .sbp): an uncompressed header, then zlib chunks of the body.

def split_file(data: bytes) -> tuple[bytes, bytes, bytes]:
    """Returns (header, chunk prefix template, body)."""
    first = data.find(struct.pack('<I', CHUNK_TAG))
    if first < 0:
        raise FormatError('no compressed body found (is this a Satisfactory save or blueprint?)')
    prefix = data[first:first + 17]
    body = bytearray()
    pos = first
    while pos < len(data):
        if struct.unpack_from('<I', data, pos)[0] != CHUNK_TAG:
            raise FormatError(f'bad chunk tag at offset {pos}')
        if data[pos:pos + 17] != prefix:
            raise FormatError(f'chunk header at offset {pos} differs from the first one')
        comp, uncomp, comp2, uncomp2 = struct.unpack_from('<qqqq', data, pos + 17)
        if comp != comp2 or uncomp != uncomp2:
            raise FormatError(f'inconsistent chunk sizes at offset {pos}')
        raw = zlib.decompress(data[pos + 49:pos + 49 + comp])
        if len(raw) != uncomp:
            raise FormatError(f'chunk at offset {pos} decompressed to the wrong size')
        body += raw
        pos += 49 + comp
    return data[:first], prefix, bytes(body)


def join_file(header: bytes, prefix: bytes, body: bytes) -> bytes:
    max_chunk = struct.unpack_from('<q', prefix, 8)[0]
    if prefix[16] != 3:
        raise FormatError(f'unknown compression algorithm {prefix[16]}')
    out = bytearray(header)
    for start in range(0, len(body), max_chunk):
        piece = body[start:start + max_chunk]
        comp = zlib.compress(piece, 6)
        out += prefix + struct.pack('<qqqq', len(comp), len(piece), len(comp), len(piece)) + comp
    return bytes(out)


# ---------------------------------------------------------------------------------------------------------------------
# Objects: one header (class, name, transform or outer) and one data blob per actor or component.

class Header:
    """type 1 = actor, 0 = component. rest = every byte after the class string, kept as read."""
    __slots__ = ('kind', 'cls', 'rest', 'level', 'path', 'outer')

    def __init__(self, kind, cls, rest, level, path, outer):
        self.kind, self.cls, self.rest, self.level, self.path, self.outer = kind, cls, rest, level, path, outer

    @staticmethod
    def read(r: Reader) -> 'Header':
        kind = r.i32()
        if kind not in (0, 1):
            raise FormatError(f'unknown object header type {kind} at offset {r.p - 4}')
        cls = r.fstr()
        start = r.p
        level = r.fstr()
        path = r.fstr()
        outer = None
        if kind == 1:
            r.raw(4 + 4 + 40 + 4)  # object flags, need-transform, rotation/position/scale, placed-in-level
        else:
            r.raw(4)  # object flags
            outer = r.fstr()
        return Header(kind, cls, r.b[start:r.p], level, path, outer)

    def write(self) -> bytes:
        return struct.pack('<i', self.kind) + fstr_bytes(self.cls) + self.rest


class Obj:
    """In a save: version, flags, size, data, and a trailing int. In a blueprint: just size and data."""
    __slots__ = ('version', 'flag', 'data', 'extra', 'in_save')

    def __init__(self, version, flag, data, extra, in_save):
        self.version, self.flag, self.data, self.extra, self.in_save = version, flag, data, extra, in_save

    @staticmethod
    def read(r: Reader, in_save: bool) -> 'Obj':
        if not in_save:
            return Obj(0, 0, r.raw(r.i32()), b'', False)
        version = r.i32()
        flag = r.i32()
        data = r.raw(r.i32())
        extra = r.raw(4)
        return Obj(version, flag, data, extra, True)

    def write(self) -> bytes:
        if not self.in_save:
            return struct.pack('<i', len(self.data)) + self.data
        return struct.pack('<iii', self.version, self.flag, len(self.data)) + self.data + self.extra


class ObjectList:
    """Headers and objects of one level (or of a blueprint), plus the unparsed tails of both blocks."""

    def __init__(self):
        self.headers: list[Header] = []
        self.objects: list[Obj] = []
        self.header_tail = b''
        self.object_tail = b''

    def read_headers(self, r: Reader, length: int):
        end = r.p + length
        count = r.i32()
        self.headers = [Header.read(r) for _ in range(count)]
        if r.p > end:
            raise FormatError('object headers overran their block')
        self.header_tail = r.raw(end - r.p)

    def read_objects(self, r: Reader, length: int, in_save: bool = True):
        end = r.p + length
        count = r.i32()
        if count != len(self.headers):
            raise FormatError(f'{count} objects for {len(self.headers)} headers')
        self.objects = [Obj.read(r, in_save) for _ in range(count)]
        if r.p > end:
            raise FormatError('objects overran their block')
        self.object_tail = r.raw(end - r.p)

    def header_block(self) -> bytes:
        return struct.pack('<i', len(self.headers)) + b''.join(h.write() for h in self.headers) + self.header_tail

    def object_block(self) -> bytes:
        return struct.pack('<i', len(self.objects)) + b''.join(o.write() for o in self.objects) + self.object_tail

    def remove(self, indices: set[int]):
        self.headers = [h for i, h in enumerate(self.headers) if i not in indices]
        self.objects = [o for i, o in enumerate(self.objects) if i not in indices]


# ---------------------------------------------------------------------------------------------------------------------
# Save body: version info, streaming grids, sub-levels, then the persistent level and a trailing table.

def read_version_container(r: Reader) -> None:
    r.raw(16)          # four version numbers
    r.raw(10)          # engine version (major, minor, patch, changelist)
    r.fstr()           # branch
    count = r.i32()
    r.raw(20 * count)  # custom versions (GUID + int)


class SaveBody:
    def __init__(self, body: bytes):
        r = Reader(body)
        size = r.i64()
        if size != len(body) - 8:
            raise FormatError(f'body size field {size} does not match {len(body) - 8}')
        start = r.p
        read_version_container(r)
        grids = r.i32()
        r.fstr(); r.raw(12); r.fstr(); r.raw(4)
        for _ in range(grids - 1):
            r.fstr(); r.raw(8)
            for _ in range(r.i32()):
                r.fstr(); r.raw(4)
        level_count = r.i32()
        self.prelude = body[start:r.p - 4]
        self.levels: list[tuple[bytes, ObjectList, bytes]] = []  # (name bytes, objects, trailer bytes)
        for index in range(level_count + 1):
            persistent = index == level_count
            name = b'' if persistent else r.fstr_raw()[1]
            objects = ObjectList()
            objects.read_headers(r, r.i64())
            objects.read_objects(r, r.i64())
            trailer = b''
            if not persistent:
                t0 = r.p
                r.raw(4)                                  # level save version
                for _ in range(r.i32()):                  # collected pickups
                    r.fstr(); r.fstr()
                if r.i32():
                    read_version_container(r)
                trailer = body[t0:r.p]
            self.levels.append((name, objects, trailer))
        self.tail = body[r.p:]

    def object_lists(self) -> list[ObjectList]:
        return [objects for _, objects, _ in self.levels]

    def write(self) -> bytes:
        out = bytearray(self.prelude)
        out += struct.pack('<i', len(self.levels) - 1)
        for name, objects, trailer in self.levels:
            headers = objects.header_block()
            objs = objects.object_block()
            out += name + struct.pack('<q', len(headers)) + headers + struct.pack('<q', len(objs)) + objs + trailer
        out += self.tail
        return struct.pack('<q', len(out)) + bytes(out)


class BlueprintBody:
    def __init__(self, body: bytes):
        r = Reader(body)
        size = r.i32()
        if size != len(body) - 4:
            raise FormatError(f'blueprint body size field {size} does not match {len(body) - 4}')
        self.objects = ObjectList()
        self.objects.read_headers(r, r.i32())
        self.objects.read_objects(r, r.i32(), in_save=False)
        self.tail = body[r.p:]

    def object_lists(self) -> list[ObjectList]:
        return [self.objects]

    def write(self) -> bytes:
        headers = self.objects.header_block()
        objs = self.objects.object_block()
        out = struct.pack('<i', len(headers)) + headers + struct.pack('<i', len(objs)) + objs + self.tail
        return struct.pack('<i', len(out)) + out


# ---------------------------------------------------------------------------------------------------------------------
# Tagged properties (Unreal 5.4+ format: the type is a tree of names).

class Prop:
    __slots__ = ('name', 'name_raw', 'type', 'type_raw', 'after_size', 'value')

    def __init__(self, name, name_raw, type_, type_raw, after_size, value):
        self.name, self.name_raw, self.type, self.type_raw, self.after_size, self.value = name, name_raw, type_, type_raw, after_size, value

    def write(self) -> bytes:
        return self.name_raw + self.type_raw + struct.pack('<i', len(self.value)) + self.after_size + self.value


def read_type(r: Reader) -> tuple:
    name = r.fstr()
    return (name, tuple(read_type(r) for _ in range(r.i32())))


def read_props(r: Reader) -> tuple[list[Prop], bytes]:
    """Reads tags up to and including 'None'; returns (properties, the 'None' bytes)."""
    props = []
    while True:
        name, name_raw = r.fstr_raw()
        if name == 'None':
            return props, name_raw
        t0 = r.p
        type_ = read_type(r)
        type_raw = r.b[t0:r.p]
        size = r.i32()
        a0 = r.p
        flags = r.u8()
        if flags & 0x01:
            r.raw(4)
        if flags & 0x02:
            r.raw(16)
        if flags & 0x04:
            ext = r.u8()
            if ext:
                raise FormatError(f'property {name}: unsupported tag extension {ext:#x}')
        if flags & ~0x3F:
            raise FormatError(f'property {name}: unknown tag flags {flags:#x}')
        after_size = r.b[a0:r.p]
        props.append(Prop(name, name_raw, type_, type_raw, after_size, r.raw(size)))


def object_ref_bytes(level: str, path: str) -> bytes:
    return fstr_bytes(level) + fstr_bytes(path)


def read_object_ref(r: Reader) -> tuple[str, str]:
    return r.fstr(), r.fstr()


class ObjectData:
    """An object's data split into prefix (actors: parent and component list), properties and native tail."""

    def __init__(self, header: Header, data: bytes):
        r = Reader(data)
        self.components: list[tuple[str, str]] | None = None
        if header.kind == 1:
            level_raw = r.fstr_raw()[1]
            self.parent = level_raw + r.fstr_raw()[1]
            self.components = [read_object_ref(r) for _ in range(r.i32())]
        else:
            self.parent = b''
        self.control = r.raw(1)
        self.props, self.none = read_props(r)
        self.tail = data[r.p:]

    def write(self) -> bytes:
        out = bytearray(self.parent)
        if self.components is not None:
            out += struct.pack('<i', len(self.components))
            for level, path in self.components:
                out += object_ref_bytes(level, path)
        out += self.control
        for prop in self.props:
            out += prop.write()
        out += self.none + self.tail
        return bytes(out)

    def find(self, name: str) -> Prop | None:
        return next((p for p in self.props if p.name == name), None)


def drop_refs_in_props(data: ObjectData, removed: set[str]) -> int:
    """Removes references to removed objects from top-level object properties and object arrays. Returns how many."""
    changes = 0
    kept = []
    for prop in data.props:
        kind = prop.type[0]
        if kind in ('ObjectProperty', 'WeakObjectProperty'):
            level, path = read_object_ref(Reader(prop.value))
            if path in removed:
                changes += 1
                continue
        elif kind == 'ArrayProperty' and prop.type[1] and prop.type[1][0][0] in ('ObjectProperty', 'WeakObjectProperty'):
            r = Reader(prop.value)
            items = [read_object_ref(r) for _ in range(r.i32())]
            left = [item for item in items if item[1] not in removed]
            if len(left) != len(items):
                changes += len(items) - len(left)
                prop.value = struct.pack('<i', len(left)) + b''.join(object_ref_bytes(*item) for item in left)
        kept.append(prop)
    data.props = kept
    return changes


# ---------------------------------------------------------------------------------------------------------------------
# The conversion.

class Report:
    def __init__(self):
        self.depots = 0
        self.wires = 0
        self.refs = 0
        self.other_mod_objects = 0
        self.notes: list[str] = []


def convert_lists(lists: list[ObjectList], report: Report) -> None:
    depots: set[str] = set()
    for objects in lists:
        for header in objects.headers:
            if header.kind == 1 and header.cls == OUR_BUILD:
                depots.add(header.path)
    connectors = {f'{path}.{OUR_CONNECTOR_NAME}' for path in depots}

    # Wires keep their two ends in the native tail of their data; any wire that mentions one of our connectors goes.
    removed: set[str] = set(connectors)
    connector_bytes = [c.encode('latin-1') for c in connectors]
    for objects in lists:
        for header, obj in zip(objects.headers, objects.objects):
            if header.kind == 1 and header.cls == POWER_LINE and any(c in obj.data for c in connector_bytes):
                removed.add(header.path)
                report.wires += 1
            elif header.cls == OUR_SUBSYSTEM:
                removed.add(header.path)
                report.other_mod_objects += 1
    # Components of removed actors (the wires' own components, if any) go with them.
    for objects in lists:
        for header in objects.headers:
            if header.kind == 0 and header.outer in removed:
                removed.add(header.path)

    removed_bytes = [p.encode('latin-1') for p in removed]
    for objects in lists:
        drop = set()
        for index, (header, obj) in enumerate(zip(objects.headers, objects.objects)):
            if header.path in removed:
                drop.add(index)
                continue
            if header.kind == 1 and header.cls == OUR_BUILD:
                data = ObjectData(header, obj.data)
                data.components = [c for c in data.components if c[1] not in removed]
                recipe = data.find('mBuiltWithRecipe')
                if recipe is not None:
                    recipe.value = object_ref_bytes('', VANILLA_RECIPE)
                report.refs += drop_refs_in_props(data, removed)
                obj.data = data.write()
                header.cls = VANILLA_BUILD
                report.depots += 1
            elif removed_bytes and any(p in obj.data for p in removed_bytes):
                data = ObjectData(header, obj.data)
                changed = drop_refs_in_props(data, removed)
                if data.components is not None:
                    before = len(data.components)
                    data.components = [c for c in data.components if c[1] not in removed]
                    changed += before - len(data.components)
                if changed:
                    report.refs += changed
                    obj.data = data.write()
                else:
                    report.notes.append(f'{header.path} still mentions a removed object outside its top-level properties; the game drops such links on load')
        objects.remove(drop)


# ---------------------------------------------------------------------------------------------------------------------
# Blueprint header: version, build, cost list, recipe list, then the rest (dimensions etc.), kept as read.

def convert_blueprint_header(header: bytes, depots: int, report: Report) -> bytes:
    r = Reader(header)
    version = r.i32()
    if version not in TESTED_BLUEPRINT_HEADER_VERSIONS and not ARGS.force:
        raise FormatError(f'blueprint header version {version} is untested (use --force to try anyway)')
    fixed = r.raw(20)  # save version, build version, dimensions x/y/z
    costs = []  # [ref bytes, item path, amount]
    for _ in range(r.i32()):
        start = r.p
        _, path = read_object_ref(r)
        costs.append([header[start:r.p], path, r.i32()])
    recipes = []  # (ref bytes, recipe path)
    for _ in range(r.i32()):
        start = r.p
        _, path = read_object_ref(r)
        recipes.append((header[start:r.p], path))
    rest = header[r.p:]
    if depots == 0:
        return header
    # Cost: a vanilla uploader instead of a Sinking Depot, per converted building.
    for entry in costs:
        entry[2] -= SINK_HALF_COST.get(entry[1], 0) * depots
    if any(entry[2] < 0 for entry in costs):
        report.notes.append('blueprint header: the cost was lower than expected; amounts that went below zero were dropped')
    costs = [entry for entry in costs if entry[2] > 0]
    # Recipes: ours becomes the vanilla uploader's (once).
    have_vanilla = any(path == VANILLA_RECIPE for _, path in recipes)
    new_recipes = []
    for raw, path in recipes:
        if path == OUR_RECIPE:
            if not have_vanilla:
                new_recipes.append(object_ref_bytes('', VANILLA_RECIPE))
                have_vanilla = True
            continue
        new_recipes.append(raw)
    out = bytearray(struct.pack('<i', version) + fixed)
    out += struct.pack('<i', len(costs))
    for raw, _, amount in costs:
        out += raw + struct.pack('<i', amount)
    out += struct.pack('<i', len(new_recipes)) + b''.join(new_recipes) + rest
    report.notes.append('blueprint header: recipe list and cost updated' + (' (removed power wires are not subtracted from the cost)' if report.wires else ''))
    return bytes(out)


# ---------------------------------------------------------------------------------------------------------------------

def check_save_header(header: bytes) -> None:
    r = Reader(header)
    header_version = r.i32()
    save_version = r.i32()
    if (header_version not in TESTED_HEADER_VERSIONS or save_version not in TESTED_SAVE_VERSIONS) and not ARGS.force:
        raise FormatError(f'save header version {header_version} / save version {save_version} is untested '
                          f'(tested: {sorted(TESTED_HEADER_VERSIONS)} / {sorted(TESTED_SAVE_VERSIONS)}); use --force to try anyway')


def convert_file(path: str, out_dir: str | None) -> bool:
    data = open(path, 'rb').read()
    header, prefix, body = split_file(data)
    is_blueprint = path.lower().endswith('.sbp')
    report = Report()
    if is_blueprint:
        parsed = BlueprintBody(body)
    else:
        check_save_header(header)
        parsed = SaveBody(body)
    # Safety: the parsed form must reproduce the original body exactly before anything is changed.
    if parsed.write() != body:
        raise FormatError('this file could not be read back exactly; nothing was written (please report it with the game version)')
    convert_lists(parsed.object_lists(), report)
    name = os.path.basename(path)
    if report.depots == 0 and report.other_mod_objects == 0:
        print(f'{name}: no Dimensional Sinking Depots found; nothing to do')
        return True
    new_body = parsed.write()
    if is_blueprint:
        header = convert_blueprint_header(header, report.depots, report)
    # Check the result parses again before writing it.
    (BlueprintBody if is_blueprint else SaveBody)(new_body)
    stem, ext = os.path.splitext(name)
    target = os.path.join(out_dir or os.path.dirname(os.path.abspath(path)), f'{stem}_vanilla{ext}')
    if os.path.exists(target) and not ARGS.overwrite_output and not ARGS.dry_run:
        raise FormatError(f'{target} already exists (delete it, or pass --overwrite-output)')
    summary = (f'{name}: {report.depots} Sinking Depot(s) -> vanilla uploaders, {report.wires} power wire(s) removed, '
               f'{report.refs} link(s) to removed objects cleaned, {report.other_mod_objects} mod bookkeeping object(s) removed')
    if ARGS.dry_run:
        print(summary + ' (dry run, nothing written)')
    else:
        with open(target, 'wb') as f:
            f.write(join_file(header, prefix, new_body))
        print(summary + f'\n  written: {target}')
    for note in report.notes:
        print(f'  note: {note}')
    return True


def main(argv: list[str]) -> int:
    global ARGS
    summary, _, details = __doc__.partition('\n\n')
    parser = argparse.ArgumentParser(description=summary, formatter_class=argparse.RawDescriptionHelpFormatter, epilog=details)
    parser.add_argument('inputs', nargs='+', help='.sav or .sbp files, or folders that contain them')
    parser.add_argument('--out-dir', help='write the converted files here instead of next to the originals')
    parser.add_argument('--force', action='store_true', help='try files from untested game versions')
    parser.add_argument('--dry-run', action='store_true', help='report what would change, write nothing')
    parser.add_argument('--overwrite-output', action='store_true', help='replace an existing *_vanilla file')
    ARGS = parser.parse_args(argv)
    files = []
    for item in ARGS.inputs:
        if os.path.isdir(item):
            for entry in sorted(os.listdir(item)):
                if entry.lower().endswith(('.sav', '.sbp')) and '_vanilla.' not in entry:
                    files.append(os.path.join(item, entry))
        else:
            files.append(item)
    if ARGS.out_dir:
        os.makedirs(ARGS.out_dir, exist_ok=True)
    failed = 0
    for path in files:
        try:
            convert_file(path, ARGS.out_dir)
        except (FormatError, OSError, zlib.error) as error:
            failed += 1
            print(f'{os.path.basename(path)}: NOT converted: {error}')
    return 1 if failed else 0


ARGS = None

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
