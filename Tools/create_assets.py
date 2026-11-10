"""Creates or updates every Dimensional Sinking Depot content asset, headlessly.

Run from the project root with the editor closed:
  UnrealEditor-Cmd.exe FactoryGame.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash

Assets (paths must match SinkingDepotConstants.h and the dev commands; renaming them breaks saves):
  /DimensionalSinkingDepot/Icons/IconDesc_SinkingDepot_512, _256   textures from Branding/ (Tools/make_icons.py)
  /DimensionalSinkingDepot/Buildable/Build_SinkingDepot           child of the vanilla uploader Build_CentralStorage_C
  /DimensionalSinkingDepot/Buildable/Desc_SinkingDepot            building descriptor (build menu placement and icons)
  /DimensionalSinkingDepot/Buildable/Recipe_SinkingDepot          build gun recipe: uploader cost + AWESOME Sink cost
  /DimensionalSinkingDepot/Schematics/Schematic_SinkingDepot      hidden custom schematic, granted by the subsystem
  /DimensionalSinkingDepot/SinkingDepotConfiguration              SML Mod Configuration (one checkbox)

Everything is updated in place, so re-running is safe. Vanilla assets are only referenced, never copied.
"""
import os

import unreal

MOD = "DimensionalSinkingDepot"
ROOT = f"/{MOD}"
LOG_TAG = f"[{MOD}]"

MOD_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRANDING_DIR = os.path.join(MOD_DIR, "Branding")

DISPLAY_NAME = "Dimensional Sinking Depot"
DESCRIPTION = (
    "A Dimensional Depot Uploader with a built-in AWESOME Sink link. Uploads items to the Dimensional Depot like the "
    "standard uploader, but items the Depot can't take any more of are sunk for AWESOME Sink points instead of backing "
    "up the belt.\nBy default it also sinks items that arrive faster than it can upload (see Mod Options)."
)

GAME = "/Game/FactoryGame"
VANILLA_BUILD = f"{GAME}/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"
BUILD_GUN = f"{GAME}/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C"
BC_ORGANIZATION = f"{GAME}/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization.BC_Organization_C"
SC_STORAGE = f"{GAME}/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Storage.SC_Storage_C"
SCHEMATIC_CATEGORY = f"{GAME}/Schematics/SchematicCategories/SC_Organisation.SC_Organisation_C"
UNLOCK_RECIPE = f"{GAME}/Unlocks/BP_UnlockRecipe.BP_UnlockRecipe_C"

# Dimensional Depot Uploader + AWESOME Sink, from the game's Docs JSON (game 1.2.5.0).
INGREDIENTS = [
    (f"{GAME}/Prototype/WAT/Desc_WAT2.Desc_WAT2_C", 1),  # Mercer Sphere
    (f"{GAME}/Resource/Parts/SAMFluctuator/Desc_SAMFluctuator.Desc_SAMFluctuator_C", 10),
    (f"{GAME}/Resource/Parts/ModularFrame/Desc_ModularFrame.Desc_ModularFrame_C", 10),
    (f"{GAME}/Resource/Parts/Wire/Desc_Wire.Desc_Wire_C", 100),
    (f"{GAME}/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced.Desc_IronPlateReinforced_C", 15),
    (f"{GAME}/Resource/Parts/Cable/Desc_Cable.Desc_Cable_C", 30),
    (f"{GAME}/Resource/Parts/Cement/Desc_Cement.Desc_Cement_C", 45),  # Concrete
]

# The vanilla uploader sits at 3 and the Personal Storage Box at 4 in Organization > Storage.
MENU_PRIORITY = 3.5

# Paint: the Blueprint keeps the vanilla default swatch. The white/FICSIT-orange default for newly placed buildings is
# applied at runtime (SinkingDepotWindow.cpp, ApplyDefaultPaint). The swatch override is cleared explicitly because
# an earlier version set it to colour slot 1.

SML_PROPS = "/SML/Interface/UI/Menu/Mods/ConfigProperties"
CONFIG_KEY = "SinkWhenUploaderCantKeepUp"  # must match SinkingDepotConstants.h
POWER_KEY = "RequirePower"  # must match SinkingDepotConstants.h


def fail(message):
    unreal.log_error(f"{LOG_TAG} {message}")
    raise RuntimeError(message)


def info(message):
    unreal.log(f"{LOG_TAG} {message}")


def must_load_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        fail(f"Could not load class {path}")
    return cls


def generated_class_of(blueprint, asset_path, name):
    try:
        cls = unreal.BlueprintEditorLibrary.generated_class(blueprint)
        if cls is not None:
            return cls
    except Exception as error:  # library may be unavailable; fall back to loading by path
        unreal.log_warning(f"{LOG_TAG} BlueprintEditorLibrary.generated_class failed: {error}")
    return unreal.load_class(None, f"{asset_path}.{name}_C")


def get_or_create_blueprint(directory, name, parent_class):
    """Loads first: in the headless commandlet the asset registry has not scanned the plugin, so does_asset_exist()
    reports False for assets that exist and create_asset() would collide with them."""
    path = f"{directory}/{name}"
    blueprint = unreal.load_asset(path)
    if blueprint is None:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, unreal.Blueprint, factory)
        if blueprint is None:
            fail(f"create_asset returned None for {path}")
        info(f"Created {path}")
    cls = generated_class_of(blueprint, path, name)
    if cls is None:
        fail(f"No generated class for {path}")
    cdo = unreal.get_default_object(cls)
    cdo.modify()
    return blueprint, cls, cdo


def save(asset, label):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        fail(f"Saving {label} failed")
    info(f"Saved {label}")


def import_texture(png_name, directory, asset_name):
    source = os.path.join(BRANDING_DIR, png_name)
    if not os.path.isfile(source):
        fail(f"{source} is missing; run Tools/make_icons.py first")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", directory)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(f"{directory}/{asset_name}")
    if texture is None:
        fail(f"Importing {source} failed")
    # Same settings as the game's own item icons: UI compression, no mips, UI texture group.
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("never_stream", True)
    save(texture, f"{directory}/{asset_name}")
    return texture


def item_amount(item_class, amount):
    value = unreal.ItemAmount()
    value.set_editor_property("item_class", item_class)
    value.set_editor_property("amount", amount)
    return value


def set_produced_in(recipe_cdo, path):
    """mProducedIn is TArray<TSoftClassPtr<UObject>>; accept whichever form this Python binding wants."""
    errors = []
    for candidate in (lambda: [must_load_class(path)], lambda: [unreal.SoftClassPath(path)]):
        try:
            recipe_cdo.set_editor_property("m_produced_in", candidate())
            return
        except Exception as error:
            errors.append(str(error))
    fail(f"Could not set m_produced_in: {errors}")


def make_icons():
    icons = f"{ROOT}/Icons"
    big = import_texture("IconDesc_SinkingDepot_512.png", icons, "IconDesc_SinkingDepot_512")
    small = import_texture("IconDesc_SinkingDepot_256.png", icons, "IconDesc_SinkingDepot_256")
    return big, small


# Power connector: a stub growing out of the machine body, like the power connector on production buildings, not a
# pole beside it. It's the Power Pole Mk1 mesh (vanilla: mesh at z 10, wire point at z 700) with its shaft hidden
# inside the body, so only the ribbed insulator head shows.
# Building space, from the vanilla uploader's component layout: the window faces +X (front), the belt input is at
# +Y (390). Seen from the front, "right" is -Y.
POWER_MESH = f"{GAME}/Buildable/Factory/PowerPoleMk1/Mesh/SM_PowerPole_Mk1"
# Measured on the real mesh: the body's back face is at x -140 (its top surface is z 317 for x >= -140 and
# y >= -400); the cage's top rail is at z ~406; the cage is X +-237, Y -465..512.
# - Placement: horizontal, out of the body's back face (-X) into the ~97 cm gap between body and cage, below the rail,
#   so it can be seen from the side. An upright stub at the top corner is hidden by the rail from every angle but
#   straight down.
# - Pitch 90 turns the pole's +Z (its top) to -X. The mesh top is 740 above its pivot (unscaled) and its base 60 below.
# - Thickness 0.55, so the head reads at a distance. Length 0.42: a uniform 0.55 pole (440 long) is longer than the
#   body is deep (x -140..140) and would poke out of the front.
# - Tip at x 106 - 0.42 x 740 = -205 (65 cm out of the body, 32 cm inside the cage); base end at 106 + 0.42 x 60 = 131,
#   inside the body.
# - y -280 keeps it clear of the ladder on the right-hand corner post.
# Footprint and stacking are unchanged from the vanilla uploader's.
POWER_MESH_LOCATION = (106.0, -280.0, 270.0)
POWER_MESH_ROTATION = dict(roll=0.0, pitch=90.0, yaw=0.0)  # keywords: unreal.Rotator's positional order is roll, pitch, yaw
POWER_MESH_SCALE = (0.55, 0.55, 0.42)  # (thickness, thickness, length): wire point at x 106 - 0.42 x 690 = -184
POWER_CONNECTION_OFFSET = (0.0, 0.0, 690.0)  # in the unscaled pole's space (vanilla: 700 above the root, mesh at 10)
# Colour: a plain StaticMeshComponent, like the depot's own MainMesh, so it's painted the same way as the body (through
# AFGBuildable::ApplyMeshPrimitiveData). As an FGColoredInstanceMeshProxy it rendered dark.
POWER_MESH_COMPONENT_CLASS = unreal.StaticMeshComponent


def find_subobject(blueprint, name):
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in sds.k2_gather_subobject_data_for_blueprint(blueprint):
        data = lib.get_data(handle)
        if str(lib.get_variable_name(data)) == name:
            return handle, lib.get_object(data)
    return None, None


def add_component(blueprint, name, component_class, parent_name=None):
    """Adds a new component to the Blueprint (or reuses the one with that name) and returns its template."""
    handle, template = find_subobject(blueprint, name)
    if template is not None:
        return template
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if parent_name:
        parent, _ = find_subobject(blueprint, parent_name)
    else:
        parent = sds.k2_gather_subobject_data_for_blueprint(blueprint)[0]  # the actor; attaches to its root
    if parent is None:
        fail(f"Parent {parent_name} not found for {name}")
    params = unreal.AddNewSubobjectParams(parent_handle=parent, new_class=component_class, blueprint_context=blueprint)
    handle, reason = sds.add_new_subobject(params)
    if not unreal.SubobjectDataBlueprintFunctionLibrary.is_handle_valid(handle):
        fail(f"Adding {name} failed: {reason}")
    sds.rename_subobject(handle, unreal.Text(name))
    template = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))
    info(f"Added component {name} [{component_class.__name__}]")
    return template


def make_build():
    blueprint, cls, cdo = get_or_create_blueprint(f"{ROOT}/Buildable", "Build_SinkingDepot", must_load_class(VANILLA_BUILD))
    cdo.set_editor_property("m_display_name", DISPLAY_NAME)
    cdo.set_editor_property("m_description", DESCRIPTION)
    cdo.set_editor_property("m_default_swatch_customization_override", None)

    # add_component reuses a component by name, class and all, so a stub of another class (the v2 proxy) is removed
    # first, together with the wire point attached to it; both are re-added below.
    _, existing = find_subobject(blueprint, "PowerConnectorMesh")
    if existing is not None and existing.get_class() != POWER_MESH_COMPONENT_CLASS.static_class():
        sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        root = sds.k2_gather_subobject_data_for_blueprint(blueprint)[0]
        for name in ("PowerConnection", "PowerConnectorMesh"):
            handle, _ = find_subobject(blueprint, name)
            if handle is not None:
                removed = sds.delete_subobject(root, handle, blueprint)
                info(f"Removed component {name} ({removed} removed) to re-add the stub as {POWER_MESH_COMPONENT_CLASS.__name__}")
        if find_subobject(blueprint, "PowerConnectorMesh")[1] is not None:
            fail("Could not remove the old PowerConnectorMesh")
    connector = add_component(blueprint, "PowerConnectorMesh", POWER_MESH_COMPONENT_CLASS)
    connector.modify()
    mesh = unreal.load_asset(POWER_MESH)
    if mesh is None:
        fail(f"{POWER_MESH} missing")
    connector.set_editor_property("static_mesh", mesh)
    connector.set_editor_property("relative_location", unreal.Vector(*POWER_MESH_LOCATION))
    connector.set_editor_property("relative_rotation", unreal.Rotator(**POWER_MESH_ROTATION))
    connector.set_editor_property("relative_scale3d", unreal.Vector(*POWER_MESH_SCALE))
    connection = add_component(blueprint, "PowerConnection", unreal.FGPowerConnectionComponent, parent_name="PowerConnectorMesh")
    connection.modify()
    connection.set_editor_property("relative_location", unreal.Vector(*POWER_CONNECTION_OFFSET))
    connection.set_editor_property("m_max_num_connection_links", 1)  # one wire, like a machine
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    save(blueprint, "Build_SinkingDepot")
    return cls


def make_descriptor(build_class, big_icon, small_icon):
    blueprint, cls, cdo = get_or_create_blueprint(f"{ROOT}/Buildable", "Desc_SinkingDepot", unreal.FGBuildingDescriptor)
    cdo.set_editor_property("m_buildable_class", build_class)
    cdo.set_editor_property("m_category", must_load_class(BC_ORGANIZATION))
    cdo.set_editor_property("m_sub_categories", [must_load_class(SC_STORAGE)])
    cdo.set_editor_property("m_menu_priority", MENU_PRIORITY)
    cdo.set_editor_property("m_small_icon", small_icon)
    cdo.set_editor_property("m_persistent_big_icon", big_icon)
    save(blueprint, "Desc_SinkingDepot")
    return cls


def make_recipe(descriptor_class):
    blueprint, cls, cdo = get_or_create_blueprint(f"{ROOT}/Buildable", "Recipe_SinkingDepot", unreal.FGRecipe)
    cdo.set_editor_property("m_display_name", DISPLAY_NAME)
    cdo.set_editor_property("m_ingredients", [item_amount(must_load_class(path), amount) for path, amount in INGREDIENTS])
    cdo.set_editor_property("m_product", [item_amount(descriptor_class, 1)])
    set_produced_in(cdo, BUILD_GUN)
    save(blueprint, "Recipe_SinkingDepot")
    return cls


def make_schematic(recipe_class, big_icon, small_icon):
    blueprint, cls, cdo = get_or_create_blueprint(f"{ROOT}/Schematics", "Schematic_SinkingDepot", unreal.FGSchematic)
    cdo.set_editor_property("m_type", unreal.SchematicType.EST_CUSTOM)
    cdo.set_editor_property("m_display_name", DISPLAY_NAME)
    cdo.set_editor_property("m_description", "Unlocked together with the Dimensional Depot research.")
    cdo.set_editor_property("m_schematic_category", must_load_class(SCHEMATIC_CATEGORY))
    cdo.set_editor_property("m_tech_tier", 0)
    cdo.set_editor_property("m_cost", [])
    cdo.set_editor_property("m_time_to_complete", 0.0)

    unlock_class = must_load_class(UNLOCK_RECIPE)
    existing = list(cdo.get_editor_property("m_unlocks") or [])
    unlock = existing[0] if existing and existing[0].get_class() == unlock_class else unreal.new_object(unlock_class, outer=cdo, name="UnlockRecipe_SinkingDepot")
    unlock.modify()
    unlock.set_editor_property("m_recipes", [recipe_class])
    cdo.set_editor_property("m_unlocks", [unlock])

    brush = unreal.SlateBrush()
    brush.set_editor_property("resource_object", big_icon)
    # UE 5.6 types FSlateBrush::ImageSize as FDeprecateSlateVector2D; only a display hint, so a failure is not fatal.
    try:
        size = unreal.DeprecateSlateVector2D()
        size.set_editor_property("x", 256.0)
        size.set_editor_property("y", 256.0)
        brush.set_editor_property("image_size", size)
    except Exception as error:
        unreal.log_warning(f"{LOG_TAG} Could not set the schematic icon size ({error}); keeping the default")
    cdo.set_editor_property("m_schematic_icon", brush)
    cdo.set_editor_property("m_small_schematic_icon", small_icon)
    save(blueprint, "Schematic_SinkingDepot")
    return cls


GRINDER_MATERIAL = "M_SinkingDepot_Grinder"


def make_grinder_material():
    """The Sinking Depot's window: an original, procedural "grinder drum" (no game art copied).

    Staggered tooth plates on dark metal, laid out in the building's local space (height for rows, the horizontal
    axes for columns), so it doesn't depend on the window's UV layout. While the building sinks, code raises Speed
    (the drum turns: rows scroll down) and Glow (orange light between the plates). Parameters are tunable per
    building through a dynamic material instance.
    """
    mel = unreal.MaterialEditingLibrary
    directory = f"{ROOT}/Materials"
    path = f"{directory}/{GRINDER_MATERIAL}"
    # Rebuilt from scratch every run: delete_all_material_expressions leaves some nodes behind (a re-run once grew
    # the graph from 50 to 74 nodes), so delete the asset and create it anew.
    existing = unreal.load_asset(path)
    if existing is not None:
        if not unreal.EditorAssetLibrary.delete_loaded_asset(existing):
            fail(f"Could not delete the old {path}")
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(GRINDER_MATERIAL, directory, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        fail(f"Could not create {path}")
    material.modify()

    def node(cls, x, y, **props):
        expression = mel.create_material_expression(material, cls, x, y)
        for key, value in props.items():
            expression.set_editor_property(key, value)
        return expression

    def link(source, target, target_input, source_output=""):
        if not mel.connect_material_expressions(source, source_output, target, target_input):
            fail(f"Could not connect {source.get_class().get_name()}.{source_output!r} -> {target.get_class().get_name()}.{target_input!r} "
                 f"(inputs: {mel.get_material_expression_input_names(target)})")

    def scalar(name, value, x, y):
        return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    def vector(name, rgb, x, y):
        param = node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgb, 1.0))
        mask = node(unreal.MaterialExpressionComponentMask, x + 200, y, r=True, g=True, b=True, a=False)
        link(param, mask, "")
        return mask

    def binary(cls, a, b, x, y):
        expression = node(cls, x, y)
        link(a, expression, "A")
        link(b, expression, "B")
        return expression

    def unary(cls, a, x, y):
        expression = node(cls, x, y)
        link(a, expression, "")
        return expression

    def step(edge, value, x, y):
        """1 where value >= edge."""
        expression = node(unreal.MaterialExpressionStep, x, y)
        link(edge, expression, "Y")
        link(value, expression, "X")
        return expression

    def lerp(a, b, alpha, x, y):
        expression = node(unreal.MaterialExpressionLinearInterpolate, x, y)
        link(a, expression, "A")
        link(b, expression, "B")
        link(alpha, expression, "Alpha")
        return expression

    # Parameters (cm, cm/s); Speed and Glow are 0 while idle.
    row_pitch = scalar("RowPitch", 14.0, -1600, -400)
    column_pitch = scalar("ColumnPitch", 24.0, -1600, -300)
    tooth = scalar("ToothFraction", 0.62, -1600, -200)
    gap = scalar("ColumnGap", 0.08, -1600, -100)
    speed = scalar("Speed", 0.0, -1600, 0)
    glow = scalar("Glow", 0.0, -1600, 100)
    plate_color = vector("PlateColor", (0.10, 0.13, 0.12), -1600, 250)
    recess_color = vector("RecessColor", (0.015, 0.02, 0.02), -1600, 400)
    glow_color = vector("GlowColor", (1.0, 0.35, 0.03), -1600, 550)
    half = node(unreal.MaterialExpressionConstant, -1600, 700, r=0.5)
    one = node(unreal.MaterialExpressionConstant, -1600, 800, r=1.0)

    local = node(unreal.MaterialExpressionLocalPosition, -1300, -600)
    height = node(unreal.MaterialExpressionComponentMask, -1100, -650, r=False, g=False, b=True, a=False)
    link(local, height, "")
    x_axis = node(unreal.MaterialExpressionComponentMask, -1100, -550, r=True, g=False, b=False, a=False)
    link(local, x_axis, "")
    y_axis = node(unreal.MaterialExpressionComponentMask, -1100, -450, r=False, g=True, b=False, a=False)
    link(local, y_axis, "")
    across = binary(unreal.MaterialExpressionAdd, x_axis, y_axis, -900, -500)  # in the window plane whichever way it faces

    # Columns, with every other column shifted half a row (staggered teeth).
    column_units = binary(unreal.MaterialExpressionDivide, across, column_pitch, -700, -500)
    column_index = unary(unreal.MaterialExpressionFloor, column_units, -500, -550)
    column_coord = unary(unreal.MaterialExpressionFrac, column_units, -500, -450)
    stagger = binary(unreal.MaterialExpressionMultiply, unary(unreal.MaterialExpressionFrac, binary(unreal.MaterialExpressionMultiply, column_index, half, -350, -600), -200, -600), one, -50, -600)

    # Rows scroll down over time while the drum turns.
    time = node(unreal.MaterialExpressionTime, -900, -300)
    travel = binary(unreal.MaterialExpressionMultiply, time, speed, -700, -300)
    moving_height = binary(unreal.MaterialExpressionAdd, height, travel, -500, -300)
    row_units = binary(unreal.MaterialExpressionDivide, moving_height, row_pitch, -300, -300)
    row_coord = unary(unreal.MaterialExpressionFrac, binary(unreal.MaterialExpressionAdd, row_units, stagger, -100, -300), 100, -300)

    # Plate mask: lower part of each row cell, inset from the column edges.
    in_row = unary(unreal.MaterialExpressionOneMinus, step(tooth, row_coord, 300, -300), 500, -300)
    column_inside = binary(unreal.MaterialExpressionMultiply, step(gap, column_coord, 300, -500),
                           step(gap, unary(unreal.MaterialExpressionOneMinus, column_coord, 150, -450), 300, -420), 500, -480)
    plate = binary(unreal.MaterialExpressionMultiply, in_row, column_inside, 700, -400)

    # Fake bevel: each plate is lit brighter towards its top edge.
    shade = binary(unreal.MaterialExpressionAdd, binary(unreal.MaterialExpressionMultiply,
                   binary(unreal.MaterialExpressionDivide, row_coord, tooth, 300, -150), half, 500, -150), half, 700, -150)
    lit_plate = binary(unreal.MaterialExpressionMultiply, plate_color, shade, 900, -150)

    base = lerp(recess_color, lit_plate, plate, 1100, -300)
    roughness = lerp(node(unreal.MaterialExpressionConstant, 900, 0, r=0.8), node(unreal.MaterialExpressionConstant, 900, 80, r=0.45), plate, 1100, 0)
    metallic = node(unreal.MaterialExpressionConstant, 1100, 150, r=0.6)
    emissive = binary(unreal.MaterialExpressionMultiply,
                      binary(unreal.MaterialExpressionMultiply, glow_color, glow, 900, 300),
                      unary(unreal.MaterialExpressionOneMinus, plate, 900, 400), 1100, 300)

    for expression, prop in ((base, unreal.MaterialProperty.MP_BASE_COLOR), (roughness, unreal.MaterialProperty.MP_ROUGHNESS),
                             (metallic, unreal.MaterialProperty.MP_METALLIC), (emissive, unreal.MaterialProperty.MP_EMISSIVE_COLOR)):
        if not mel.connect_material_property(expression, "", prop):
            fail(f"Could not connect {prop}")
    mel.recompile_material(material)
    save(material, f"{directory}/{GRINDER_MATERIAL}")
    return material


def make_config():
    blueprint, cls, cdo = get_or_create_blueprint(ROOT, "SinkingDepotConfiguration", unreal.ModConfiguration)
    section_cls = must_load_class(f"{SML_PROPS}/BP_ConfigPropertySection.BP_ConfigPropertySection_C")
    bool_cls = must_load_class(f"{SML_PROPS}/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C")

    root = cdo.get_editor_property("root_section")
    if root is None or root.get_class() != section_cls:
        root = unreal.new_object(section_cls, outer=cdo, name="RootSection")
    root.modify()
    existing_props = root.get_editor_property("section_properties") or {}
    host_note = "In multiplayer the host's or server's setting is used."
    specs = [
        (CONFIG_KEY, "Sink overflow even if the Depot has free space", True, (
            "On: items that arrive faster than a Dimensional Sinking Depot can upload are sunk, so the belt never backs up. "
            "Some of them could have been uploaded later.\n"
            "Off: the belt backs up like a normal uploader until the Depot is full for that item; only then is the overflow sunk.\n"
            "Items the Depot is already full of are always sunk.\n" + host_note)),
        (POWER_KEY, "Require power to sink (like an AWESOME Sink)", False, (
            "Off: the built-in sink is free and needs no power.\n"
            "On: a Dimensional Sinking Depot only sinks while it has power, and draws what an AWESOME Sink draws (30 MW) while sinking. "
            "Without power it still uploads, like a normal uploader, and the overflow backs up.\n"
            "Connect power to the connector at its back corner.\n" + host_note)),
    ]
    section_properties = {}
    for key, display_name, default, tooltip in specs:
        existing = existing_props.get(key)
        prop = existing if existing is not None and existing.get_class() == bool_cls else unreal.new_object(bool_cls, outer=root, name=key)
        prop.modify()
        prop.set_editor_property("display_name", display_name)
        prop.set_editor_property("tooltip", tooltip)
        prop.set_editor_property("default_value", default)
        prop.set_editor_property("value", default)
        prop.set_editor_property("requires_world_reload", False)
        section_properties[key] = prop
    root.set_editor_property("section_properties", section_properties)
    root.set_editor_property("widget_type", unreal.CP_SectionWidgetType.CPS_VERTICAL)

    config_id = unreal.ConfigId()
    config_id.set_editor_property("mod_reference", MOD)
    config_id.set_editor_property("config_category", "")
    cdo.set_editor_property("config_id", config_id)
    cdo.set_editor_property("display_name", DISPLAY_NAME)
    cdo.set_editor_property("description", "How the Dimensional Sinking Depot handles items it can't upload right away.")
    cdo.set_editor_property("root_section", root)
    save(blueprint, "SinkingDepotConfiguration")


def main():
    big_icon, small_icon = make_icons()
    make_grinder_material()
    build_class = make_build()
    descriptor_class = make_descriptor(build_class, big_icon, small_icon)
    recipe_class = make_recipe(descriptor_class)
    make_schematic(recipe_class, big_icon, small_icon)
    make_config()
    info("All assets created or updated")


main()
