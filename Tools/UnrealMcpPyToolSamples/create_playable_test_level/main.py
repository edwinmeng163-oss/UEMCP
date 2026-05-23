# create_playable_test_level.py
# Sample Python user tool. Copy this directory to <ProjectDir>/Tools/UnrealMcpPyTools/
# and run unreal.mcp_user_registry_reload before use.

TOOL_NAME = "user.create_playable_test_level"
DEFAULT_LEVEL_NAME = "UEAtelier_PlayableTestLevel"
DEFAULT_GROUND_SIZE = 4000.0
DEFAULT_CHARACTER_LABEL = "UEAtelier_TestCharacter"

THIRD_PERSON_CLASS_CANDIDATES = [
    "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C",
    "/Game/Characters/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C",
]
BASIC_CHARACTER_CLASS = "/Script/Engine.Character"


def _bool(value, default=False):
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.lower() in ("1", "true", "yes", "on")
    return bool(value)


def _string(value, default=""):
    if value is None:
        return default
    return str(value).strip()


def _float(value, default):
    try:
        return float(value)
    except Exception:
        return float(default)


def _safe_asset_name(value):
    raw = _string(value, DEFAULT_LEVEL_NAME) or DEFAULT_LEVEL_NAME
    chars = []
    for char in raw:
        if char.isalnum() or char == "_":
            chars.append(char)
        elif char in ("-", " ", "."):
            chars.append("_")
    name = "".join(chars).strip("_")
    if not name:
        name = DEFAULT_LEVEL_NAME
    return name[:64]


def _level_package_path(level_name):
    return "/Game/levels/%s" % _safe_asset_name(level_name)


def _object_path(obj):
    if obj is None:
        return ""
    for method_name in ("get_path_name", "get_name"):
        method = getattr(obj, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    return str(obj)


def _package_info(obj):
    if obj is None:
        return {"available": False, "path": "", "name": ""}

    package = None
    for method_name in ("get_outermost", "get_package"):
        method = getattr(obj, method_name, None)
        if callable(method):
            try:
                package = method()
                if package:
                    break
            except Exception:
                pass

    return {
        "available": bool(package),
        "path": _object_path(package),
        "name": _object_path(package),
    }


def _get_subsystem(unreal, subsystem_name):
    subsystem_class = getattr(unreal, subsystem_name, None)
    if not subsystem_class:
        return None
    try:
        return unreal.get_editor_subsystem(subsystem_class)
    except Exception:
        return None


def _get_editor_world(unreal):
    editor_subsystem = _get_subsystem(unreal, "UnrealEditorSubsystem")
    method = getattr(editor_subsystem, "get_editor_world", None)
    if callable(method):
        try:
            return method()
        except Exception:
            return None
    return None


def _actor_subsystem(unreal):
    return _get_subsystem(unreal, "EditorActorSubsystem")


def _load_class(unreal, class_path, fallback_name=""):
    try:
        cls = unreal.load_class(None, class_path)
        if cls:
            return cls
    except Exception:
        pass
    if fallback_name:
        return getattr(unreal, fallback_name, None)
    return None


def _load_object(unreal, object_path):
    try:
        return unreal.load_object(None, object_path)
    except Exception:
        return None


def _asset_exists(unreal, package_path):
    asset_name = package_path.rsplit("/", 1)[-1]
    object_path = "%s.%s" % (package_path, asset_name)
    candidates = [package_path, object_path]

    asset_subsystem = _get_subsystem(unreal, "EditorAssetSubsystem")
    method = getattr(asset_subsystem, "does_asset_exist", None)
    if callable(method):
        for candidate in candidates:
            try:
                if method(candidate):
                    return {"checked": True, "exists": True, "method": "EditorAssetSubsystem.does_asset_exist", "path": candidate}
            except Exception:
                pass
        return {"checked": True, "exists": False, "method": "EditorAssetSubsystem.does_asset_exist", "path": package_path}

    asset_library = getattr(unreal, "EditorAssetLibrary", None)
    method = getattr(asset_library, "does_asset_exist", None)
    if callable(method):
        for candidate in candidates:
            try:
                if method(candidate):
                    return {"checked": True, "exists": True, "method": "EditorAssetLibrary.does_asset_exist", "path": candidate}
            except Exception:
                pass
        return {"checked": True, "exists": False, "method": "EditorAssetLibrary.does_asset_exist", "path": package_path}

    return {"checked": False, "exists": False, "method": "", "path": package_path}


def _vector(unreal, x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def _rotator(unreal, pitch=0.0, yaw=0.0, roll=0.0):
    return unreal.Rotator(float(pitch), float(yaw), float(roll))


def _try_set_property(obj, prop, value):
    method = getattr(obj, "set_editor_property", None)
    if callable(method):
        try:
            method(prop, value)
            return True, ""
        except Exception as exc:
            return False, str(exc)
    return False, "set_editor_property is unavailable"


def _mark_dirty(obj):
    for method_name in ("modify", "mark_package_dirty"):
        method = getattr(obj, method_name, None)
        if callable(method):
            try:
                method()
            except Exception:
                pass
    package = None
    for method_name in ("get_outermost", "get_package"):
        method = getattr(obj, method_name, None)
        if callable(method):
            try:
                package = method()
                if package:
                    break
            except Exception:
                pass
    set_dirty = getattr(package, "set_dirty_flag", None)
    if callable(set_dirty):
        try:
            set_dirty(True)
        except Exception:
            pass


def _spawn_actor(unreal, actor_subsystem, class_path, fallback_name, location, rotation, label):
    cls = _load_class(unreal, class_path, fallback_name)
    if not cls:
        return None, {"label": label, "classPath": class_path, "error": "class unavailable"}
    try:
        actor = actor_subsystem.spawn_actor_from_class(cls, location, rotation)
    except Exception as exc:
        return None, {"label": label, "classPath": class_path, "error": str(exc)}
    if not actor:
        return None, {"label": label, "classPath": class_path, "error": "spawn returned no actor"}
    try:
        actor.set_actor_label(label)
    except Exception:
        pass
    _mark_dirty(actor)
    return actor, {"label": label, "classPath": class_path, "path": _object_path(actor)}


def _set_static_mesh(unreal, actor, mesh_path):
    mesh = _load_object(unreal, mesh_path)
    if not mesh:
        return {"meshPath": mesh_path, "set": False, "reason": "mesh unavailable"}
    component_class = getattr(unreal, "StaticMeshComponent", None)
    component = None
    if component_class:
        method = getattr(actor, "get_component_by_class", None)
        if callable(method):
            try:
                component = method(component_class)
            except Exception:
                component = None
    if not component:
        return {"meshPath": mesh_path, "set": False, "reason": "StaticMeshComponent unavailable"}
    set_mesh = getattr(component, "set_static_mesh", None)
    if callable(set_mesh):
        try:
            set_mesh(mesh)
            return {"meshPath": mesh_path, "set": True}
        except Exception as exc:
            return {"meshPath": mesh_path, "set": False, "reason": str(exc)}
    ok, error = _try_set_property(component, "static_mesh", mesh)
    return {"meshPath": mesh_path, "set": ok, "reason": error}


def _set_actor_scale(actor, unreal, x, y, z):
    method = getattr(actor, "set_actor_scale3d", None)
    if callable(method):
        try:
            method(unreal.Vector(float(x), float(y), float(z)))
            return True
        except Exception:
            return False
    return False


def _configure_light_component(unreal, actor, component_name, properties):
    component_class = getattr(unreal, component_name, None)
    if not component_class:
        return {"componentClass": component_name, "configured": False, "reason": "component class unavailable"}
    component = None
    method = getattr(actor, "get_component_by_class", None)
    if callable(method):
        try:
            component = method(component_class)
        except Exception:
            component = None
    if not component:
        return {"componentClass": component_name, "configured": False, "reason": "component unavailable"}
    result = {"componentClass": component_name, "configured": True, "properties": []}
    for prop, value in properties.items():
        ok, error = _try_set_property(component, prop, value)
        result["properties"].append({"name": prop, "set": ok, "error": error})
    return result


def _choose_character_class(unreal, requested_path):
    candidates = []
    if requested_path:
        candidates.append({"path": requested_path, "kind": "user_supplied"})
    for path in THIRD_PERSON_CLASS_CANDIDATES:
        candidates.append({"path": path, "kind": "third_person_candidate"})
    candidates.append({"path": BASIC_CHARACTER_CLASS, "kind": "basic_character_fallback"})

    checked = []
    for candidate in candidates:
        cls = _load_class(unreal, candidate["path"])
        checked.append({"path": candidate["path"], "kind": candidate["kind"], "found": bool(cls)})
        if cls:
            return cls, candidate["path"], candidate["kind"], checked
    return None, "", "", checked


def _configure_character(unreal, actor, class_kind):
    changes = []
    warnings = []
    auto_receive = getattr(unreal, "AutoReceiveInput", None)
    if auto_receive and hasattr(auto_receive, "PLAYER0"):
        ok, error = _try_set_property(actor, "auto_possess_player", auto_receive.PLAYER0)
        if ok:
            changes.append({"target": "auto_possess_player", "value": "PLAYER0"})
        else:
            warnings.append({"target": "auto_possess_player", "warning": error})

    movement_class = getattr(unreal, "CharacterMovementComponent", None)
    movement = None
    if movement_class:
        method = getattr(actor, "get_component_by_class", None)
        if callable(method):
            try:
                movement = method(movement_class)
            except Exception:
                movement = None
    if movement:
        for prop, value in (("max_walk_speed", 600.0), ("jump_z_velocity", 520.0), ("air_control", 0.35)):
            ok, error = _try_set_property(movement, prop, value)
            if ok:
                changes.append({"target": "CharacterMovement.%s" % prop, "value": value})
            else:
                warnings.append({"target": "CharacterMovement.%s" % prop, "warning": error})

    return {
        "classKind": class_kind,
        "fallbackReported": class_kind != "third_person_candidate",
        "changes": changes,
        "warnings": warnings,
    }


def _create_new_level(unreal, level_path):
    level_subsystem = _get_subsystem(unreal, "LevelEditorSubsystem")
    new_level = getattr(level_subsystem, "new_level", None)
    if callable(new_level):
        try:
            result = new_level(level_path)
            return bool(result) if result is not None else True, "LevelEditorSubsystem.new_level", ""
        except Exception as exc:
            return False, "LevelEditorSubsystem.new_level", str(exc)
    return False, "", "LevelEditorSubsystem.new_level is unavailable"


def _save_level(unreal):
    results = []
    level_subsystem = _get_subsystem(unreal, "LevelEditorSubsystem")
    save_current = getattr(level_subsystem, "save_current_level", None)
    if callable(save_current):
        try:
            results.append({"method": "LevelEditorSubsystem.save_current_level", "saved": bool(save_current())})
        except Exception as exc:
            results.append({"method": "LevelEditorSubsystem.save_current_level", "saved": False, "error": str(exc)})

    save_utils = getattr(unreal, "EditorLoadingAndSavingUtils", None)
    save_dirty = getattr(save_utils, "save_dirty_packages", None)
    if callable(save_dirty):
        try:
            results.append({"method": "EditorLoadingAndSavingUtils.save_dirty_packages", "saved": bool(save_dirty(True, True))})
        except Exception as exc:
            results.append({"method": "EditorLoadingAndSavingUtils.save_dirty_packages", "saved": False, "error": str(exc)})

    return results or [{"method": "", "saved": False, "error": "No non-deprecated save API is available."}]


def _verify_labels(actor_subsystem, expected_labels):
    found = {}
    try:
        actors = actor_subsystem.get_all_level_actors()
    except Exception as exc:
        return {"verified": False, "error": str(exc), "labels": []}
    labels = []
    for actor in actors:
        try:
            label = actor.get_actor_label()
        except Exception:
            label = ""
        if label:
            labels.append(label)
            if label in expected_labels:
                found[label] = _object_path(actor)
    missing = [label for label in expected_labels if label not in found]
    return {"verified": not missing, "found": found, "missing": missing, "labels": labels}


def _plan(args):
    level_name = _safe_asset_name(args.get("levelName"))
    level_path = _level_package_path(level_name)
    include_character = _bool(args.get("includeCharacter"), True)
    ground_size = max(500.0, _float(args.get("groundSize"), DEFAULT_GROUND_SIZE))
    character_label = _string(args.get("characterLabel"), DEFAULT_CHARACTER_LABEL) or DEFAULT_CHARACTER_LABEL
    character_class_path = _string(args.get("characterClassPath"))
    would_write = [
        {"target": level_path, "operation": "create_new_blank_level"},
        {"target": "UEAtelier_Ground", "operation": "spawn_static_mesh_floor", "sizeCm": ground_size},
        {"target": "UEAtelier_DirectionalLight", "operation": "spawn_directional_light"},
        {"target": "UEAtelier_SkyLight", "operation": "spawn_sky_light"},
        {"target": "UEAtelier_PlayerStart", "operation": "spawn_player_start"},
        {"target": character_label, "operation": "spawn_optional_character", "enabled": include_character},
        {"target": level_path, "operation": "save_and_verify_actor_presence"},
    ]
    return level_name, level_path, include_character, ground_size, character_label, character_class_path, would_write


def execute(args):
    args = args or {}
    dry_run = _bool(args.get("dryRun"), True)
    level_name, level_path, include_character, ground_size, character_label, character_class_path, would_write = _plan(args)

    structured = {
        "action": "create_playable_test_level",
        "toolName": TOOL_NAME,
        "dryRun": dry_run,
        "levelName": level_name,
        "levelPath": level_path,
        "includeCharacter": include_character,
        "characterLabel": character_label,
        "characterClassPath": character_class_path,
        "groundSize": ground_size,
        "protectedMaps": ["/Game/TopDown/Lvl_TopDown"],
    }

    if level_name.lower() == "lvl_topdown":
        structured["wouldWrite"] = would_write
        return {"isError": True, "text": "Refusing to create or mutate a TopDown-sensitive level name.", "structuredContent": structured}

    if dry_run:
        structured["wouldWrite"] = would_write
        structured["notes"] = [
            "Dry run only; no map or actors were created.",
            "The real run creates a new map under /Game/levels/ and never opens or edits /Game/TopDown/Lvl_TopDown.",
        ]
        return {"text": "Dry run: would create playable test level %s." % level_path, "structuredContent": structured}

    try:
        import unreal
    except Exception as exc:
        structured["wouldWrite"] = would_write
        return {"isError": True, "text": "Unreal Python module is not available: %s" % exc, "structuredContent": structured}

    spawned = []
    warnings = []
    try:
        exists = _asset_exists(unreal, level_path)
        structured["assetExists"] = exists
        if exists.get("exists"):
            structured["wouldWrite"] = would_write
            return {
                "isError": True,
                "text": "Refusing to overwrite existing level asset %s." % exists.get("path", level_path),
                "structuredContent": structured,
            }

        created, method, error = _create_new_level(unreal, level_path)
        structured["levelCreation"] = {"created": created, "method": method, "error": error}
        if not created:
            structured["wouldWrite"] = would_write
            return {"isError": True, "text": "Failed to create new level %s: %s" % (level_path, error), "structuredContent": structured}

        world = _get_editor_world(unreal)
        actor_subsystem = _actor_subsystem(unreal)
        if not world:
            raise RuntimeError("No editor world is active after level creation.")
        if not actor_subsystem:
            raise RuntimeError("EditorActorSubsystem is unavailable.")

        ground_location = _vector(unreal, 0.0, 0.0, -10.0)
        ground, ground_info = _spawn_actor(
            unreal,
            actor_subsystem,
            "/Script/Engine.StaticMeshActor",
            "StaticMeshActor",
            ground_location,
            _rotator(unreal),
            "UEAtelier_Ground",
        )
        spawned.append(ground_info)
        if ground:
            mesh_result = _set_static_mesh(unreal, ground, "/Engine/BasicShapes/Cube.Cube")
            scale = ground_size / 100.0
            _set_actor_scale(ground, unreal, scale, scale, 0.2)
            spawned[-1]["mesh"] = mesh_result
            spawned[-1]["scale"] = {"x": scale, "y": scale, "z": 0.2}

        directional, directional_info = _spawn_actor(
            unreal,
            actor_subsystem,
            "/Script/Engine.DirectionalLight",
            "DirectionalLight",
            _vector(unreal, -500.0, -500.0, 800.0),
            _rotator(unreal, -45.0, -35.0, 0.0),
            "UEAtelier_DirectionalLight",
        )
        spawned.append(directional_info)
        if directional:
            spawned[-1]["light"] = _configure_light_component(
                unreal,
                directional,
                "DirectionalLightComponent",
                {"intensity": 4.0},
            )

        skylight, skylight_info = _spawn_actor(
            unreal,
            actor_subsystem,
            "/Script/Engine.SkyLight",
            "SkyLight",
            _vector(unreal, 0.0, 0.0, 300.0),
            _rotator(unreal),
            "UEAtelier_SkyLight",
        )
        spawned.append(skylight_info)
        if skylight:
            spawned[-1]["light"] = _configure_light_component(
                unreal,
                skylight,
                "SkyLightComponent",
                {"intensity": 1.0},
            )

        sky_atmosphere, sky_info = _spawn_actor(
            unreal,
            actor_subsystem,
            "/Script/Engine.SkyAtmosphere",
            "SkyAtmosphere",
            _vector(unreal, 0.0, 0.0, 0.0),
            _rotator(unreal),
            "UEAtelier_SkyAtmosphere",
        )
        if sky_atmosphere:
            spawned.append(sky_info)
        elif sky_info.get("error"):
            warnings.append({"target": "UEAtelier_SkyAtmosphere", "warning": sky_info.get("error")})

        player_start, player_info = _spawn_actor(
            unreal,
            actor_subsystem,
            "/Script/Engine.PlayerStart",
            "PlayerStart",
            _vector(unreal, -250.0, 0.0, 120.0),
            _rotator(unreal, 0.0, 0.0, 0.0),
            "UEAtelier_PlayerStart",
        )
        spawned.append(player_info)

        character_info = None
        if include_character:
            character_class, selected_path, class_kind, checked_classes = _choose_character_class(unreal, character_class_path)
            structured["characterClassCandidates"] = checked_classes
            if character_class:
                character, character_info = _spawn_actor(
                    unreal,
                    actor_subsystem,
                    selected_path,
                    "",
                    _vector(unreal, 0.0, 0.0, 110.0),
                    _rotator(unreal),
                    character_label,
                )
                if character:
                    config = _configure_character(unreal, character, class_kind)
                    character_info["selectedClass"] = selected_path
                    character_info["selectedClassKind"] = class_kind
                    character_info["configuration"] = config
                    spawned.append(character_info)
                else:
                    warnings.append({"target": character_label, "warning": "Character spawn failed."})
            else:
                warnings.append({"target": character_label, "warning": "No character class was available."})

        _mark_dirty(world)
        save_results = _save_level(unreal)
        expected_labels = ["UEAtelier_Ground", "UEAtelier_DirectionalLight", "UEAtelier_SkyLight", "UEAtelier_PlayerStart"]
        if include_character:
            expected_labels.append(character_label)
        verification = _verify_labels(actor_subsystem, expected_labels)

        structured["worldPackage"] = _package_info(world)
        structured["spawned"] = spawned
        structured["warnings"] = warnings
        structured["save"] = save_results
        structured["verification"] = verification
        structured["mapCheckExpectation"] = "Ground, lighting, PlayerStart, and optional character should exist before a separate map-check or PIE smoke run."
        text = "Created playable test level %s." % level_path
        if not verification.get("verified"):
            text = "Created playable test level %s, but actor-presence verification reported missing labels." % level_path
        return {"text": text, "structuredContent": structured}
    except Exception as exc:
        structured["spawned"] = spawned
        structured["warnings"] = warnings
        structured["wouldWrite"] = would_write
        return {"isError": True, "text": "Failed to create playable test level: %s" % exc, "structuredContent": structured}
