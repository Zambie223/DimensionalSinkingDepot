"""Loads every Dimensional Sinking Depot asset in a fresh editor process and logs what was saved.

Run headless from the project root (the editor must not be open):
  UnrealEditor-Cmd.exe FactoryGame.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash
Every line is tagged "[DimensionalSinkingDepot] VERIFY"; any problem is logged as an error and fails the run.
"""
import unreal

TAG = "[DimensionalSinkingDepot] VERIFY"
ROOT = "/DimensionalSinkingDepot"
problems = []


def log(message):
    unreal.log(f"{TAG} {message}")


def problem(message):
    problems.append(message)
    unreal.log_error(f"{TAG} PROBLEM: {message}")


def cdo_of(path):
    name = path.rsplit("/", 1)[1]
    cls = unreal.load_class(None, f"{path}.{name}_C")
    if cls is None:
        problem(f"class {path} not found")
        return None, None
    return cls, unreal.get_default_object(cls)


def name_of(obj):
    return obj.get_path_name() if obj is not None else "None"


for icon in ("IconDesc_SinkingDepot_512", "IconDesc_SinkingDepot_256"):
    texture = unreal.load_asset(f"{ROOT}/Icons/{icon}")
    if texture is None:
        problem(f"texture {icon} missing")
    else:
        log(f"{icon}: {texture.blueprint_get_size_x()}x{texture.blueprint_get_size_y()} compression={texture.get_editor_property('compression_settings')} "
            f"mips={texture.get_editor_property('mip_gen_settings')} group={texture.get_editor_property('lod_group')}")

build_cls, build = cdo_of(f"{ROOT}/Buildable/Build_SinkingDepot")
if build is not None:
    expected_parent = "/Game/FactoryGame/Buildable/Factory/CentralStorage/Build_CentralStorage.Build_CentralStorage_C"
    # Blueprint.parent_class is not exposed to Python; ClassIsChildOf answers the same question.
    vanilla = unreal.load_class(None, expected_parent)
    is_child = vanilla is not None and unreal.MathLibrary.class_is_child_of(build_cls, vanilla)
    log(f"Build child of {expected_parent}: {is_child}; name='{build.get_editor_property('m_display_name')}'")
    if not is_child:
        problem(f"Build_SinkingDepot is not a child of {expected_parent}")
    swatch = build.get_editor_property("m_default_swatch_customization_override")
    log(f"Build default swatch={name_of(swatch)}")
    if swatch is not None:
        problem("Build default swatch should be empty (standard FICSIT orange)")
    if str(build.get_editor_property("m_display_name")) != "Dimensional Sinking Depot":
        problem("Build display name wrong")

desc_cls, desc = cdo_of(f"{ROOT}/Buildable/Desc_SinkingDepot")
if desc is not None:
    log(f"Desc buildable={name_of(desc.get_editor_property('m_buildable_class'))} category={name_of(desc.get_editor_property('m_category'))} "
        f"sub={[name_of(c) for c in desc.get_editor_property('m_sub_categories')]} priority={desc.get_editor_property('m_menu_priority')} "
        f"icons={name_of(desc.get_editor_property('m_small_icon'))}, {name_of(desc.get_editor_property('m_persistent_big_icon'))}")
    if desc.get_editor_property("m_buildable_class") != build_cls:
        problem("Desc does not point at Build_SinkingDepot")

recipe_cls, recipe = cdo_of(f"{ROOT}/Buildable/Recipe_SinkingDepot")
if recipe is not None:
    ingredients = [(name_of(a.get_editor_property("item_class")).rsplit(".", 1)[-1], a.get_editor_property("amount")) for a in recipe.get_editor_property("m_ingredients")]
    products = [(name_of(a.get_editor_property("item_class")).rsplit(".", 1)[-1], a.get_editor_property("amount")) for a in recipe.get_editor_property("m_product")]
    log(f"Recipe name='{recipe.get_editor_property('m_display_name')}' ingredients={ingredients} product={products} "
        f"produced_in={[str(p) for p in recipe.get_editor_property('m_produced_in')]}")
    if len(ingredients) != 7:
        problem("Recipe should have 7 ingredients")
    if not recipe.get_editor_property("m_produced_in"):
        problem("Recipe has no producer (build gun)")

schematic_cls, schematic = cdo_of(f"{ROOT}/Schematics/Schematic_SinkingDepot")
if schematic is not None:
    unlocks = schematic.get_editor_property("m_unlocks")
    unlock_info = [(u.get_class().get_name(), [name_of(r) for r in u.get_editor_property("m_recipes")], name_of(u.get_outer())) for u in unlocks]
    brush = schematic.get_editor_property("m_schematic_icon")
    log(f"Schematic type={schematic.get_editor_property('m_type')} name='{schematic.get_editor_property('m_display_name')}' unlocks={unlock_info} "
        f"icon={name_of(brush.get_editor_property('resource_object'))} size={brush.get_editor_property('image_size')}")
    if not unlocks or recipe_cls is None or recipe_cls not in list(unlocks[0].get_editor_property("m_recipes")):
        problem("Schematic does not unlock Recipe_SinkingDepot")

grinder = unreal.load_asset(f"{ROOT}/Materials/M_SinkingDepot_Grinder")
if grinder is None:
    problem("material M_SinkingDepot_Grinder missing")
else:
    mel = unreal.MaterialEditingLibrary
    scalars = sorted(str(n) for n in mel.get_scalar_parameter_names(grinder))
    vectors = sorted(str(n) for n in mel.get_vector_parameter_names(grinder))
    log(f"Grinder material: {mel.get_num_material_expressions(grinder)} nodes, scalars={scalars}, vectors={vectors}")
    if mel.get_num_material_expressions(grinder) != 50:
        problem(f"Grinder material has {mel.get_num_material_expressions(grinder)} nodes; the script builds 50 (leftovers from an old graph?)")
    for needed in ("Speed", "Glow"):
        if needed not in scalars:
            problem(f"Grinder material lacks scalar parameter {needed} (the code drives it)")

config_cls, config = cdo_of(f"{ROOT}/SinkingDepotConfiguration")
if config is not None:
    config_id = config.get_editor_property("config_id")
    root = config.get_editor_property("root_section")
    props = root.get_editor_property("section_properties") if root is not None else {}
    log(f"Config id='{config_id.get_editor_property('mod_reference')}' props={[(k, v.get_class().get_name(), v.get_editor_property('default_value')) for k, v in props.items()]}")
    for key, default in (("SinkWhenUploaderCantKeepUp", True), ("RequirePower", False)):
        if key not in props:
            problem(f"Config key {key} missing")
        elif props[key].get_editor_property("default_value") != default:
            problem(f"Config key {key} default should be {default}")

if build is not None:
    # The power connector components added to the Blueprint (attached under the inherited root).
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    found = {}
    for handle in sds.k2_gather_subobject_data_for_blueprint(unreal.load_asset(f"{ROOT}/Buildable/Build_SinkingDepot")):
        data = lib.get_data(handle)
        obj = lib.get_object(data)
        if obj is not None:
            found[str(lib.get_variable_name(data))] = obj
    mesh_comp = found.get("PowerConnectorMesh")
    conn = found.get("PowerConnection")
    log(f"Power connector: mesh={name_of(mesh_comp.get_editor_property('static_mesh')) if mesh_comp else None} "
        f"loc={mesh_comp.get_editor_property('relative_location') if mesh_comp else None} "
        f"rot={mesh_comp.get_editor_property('relative_rotation') if mesh_comp else None} "
        f"scale={mesh_comp.get_editor_property('relative_scale3d') if mesh_comp else None} "
        f"class={mesh_comp.get_class().get_name() if mesh_comp else None} "
        f"connection={conn.get_class().get_name() if conn else None} links={conn.get_editor_property('m_max_num_connection_links') if conn else None}")
    if mesh_comp is None or conn is None:
        problem("Power connector components missing from Build_SinkingDepot")
    elif mesh_comp.get_class().get_name() != "StaticMeshComponent":
        problem(f"PowerConnectorMesh is {mesh_comp.get_class().get_name()}, expected a plain StaticMeshComponent (painted like MainMesh)")
    elif conn.get_attach_parent() is not None and conn.get_attach_parent() != mesh_comp:
        problem("PowerConnection is not attached to PowerConnectorMesh")

log(f"done, {len(problems)} problem(s)")
if problems:
    raise RuntimeError(f"{len(problems)} problem(s)")
