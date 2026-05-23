# create_third_person_ground_character.py
# Project-local Python user tool. Loaded by unreal.mcp_user_registry_reload.

TOOL_NAME = "user.create_third_person_ground_character"
DEFAULT_LABEL = "UEAtelier_ThirdPersonCharacter"
DEFAULT_LOCATION = {"x": 0.0, "y": 0.0, "z": 110.0}
DEFAULT_CHARACTER_POLICY = "preferThirdPerson"

THIRD_PERSON_CLASS_CANDIDATES = [
    "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C",
    "/Game/Characters/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C",
]
BASIC_CHARACTER_CLASS = "/Script/Engine.Character"
VALID_CHARACTER_POLICIES = {
    "preferThirdPerson",
    "requireThirdPerson",
    "basicCharacterFallback",
}


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


def _location(args):
    raw = args.get("location") or {}
    if not isinstance(raw, dict):
        raw = {}
    return {
        "x": float(raw.get("x", DEFAULT_LOCATION["x"])),
        "y": float(raw.get("y", DEFAULT_LOCATION["y"])),
        "z": float(raw.get("z", DEFAULT_LOCATION["z"])),
    }


def _character_policy(args):
    policy = _string(args.get("characterClassPolicy"), DEFAULT_CHARACTER_POLICY)
    if policy not in VALID_CHARACTER_POLICIES:
        return DEFAULT_CHARACTER_POLICY
    return policy


def _vector(unreal, values):
    return unreal.Vector(values["x"], values["y"], values["z"])


def _rotator(unreal, pitch=0.0, yaw=0.0, roll=0.0):
    return unreal.Rotator(float(pitch), float(yaw), float(roll))


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


def _external_package_info(actor):
    method = getattr(actor, "get_external_package", None)
    package = None
    if callable(method):
        try:
            package = method()
        except Exception:
            package = None
    info = _package_info(package) if package else {"available": False, "path": "", "name": ""}
    is_external = getattr(actor, "is_package_external", None)
    if callable(is_external):
        try:
            info["isPackageExternal"] = bool(is_external())
        except Exception:
            info["isPackageExternal"] = False
    else:
        info["isPackageExternal"] = bool(package)
    return info


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


def _load_class(unreal, class_path):
    try:
        return unreal.load_class(None, class_path)
    except Exception:
        return None


def _load_character_class(unreal, args):
    policy = _character_policy(args)
    user_class_path = _string(args.get("classPath"))

    candidates = []
    if user_class_path:
        candidates.append({"path": user_class_path, "kind": "user_supplied"})
    for path in THIRD_PERSON_CLASS_CANDIDATES:
        candidates.append({"path": path, "kind": "third_person_candidate"})
    if policy != "requireThirdPerson":
        candidates.append({"path": BASIC_CHARACTER_CLASS, "kind": "basic_character_fallback"})

    checked = []
    for candidate in candidates:
        cls = _load_class(unreal, candidate["path"])
        checked.append({"path": candidate["path"], "kind": candidate["kind"], "found": bool(cls)})
        if cls:
            return cls, candidate["path"], candidate["kind"], checked, policy
    return None, "", "", checked, policy


def _find_actor_by_label(actor_subsystem, label):
    if not actor_subsystem:
        return None
    try:
        actors = actor_subsystem.get_all_level_actors()
    except Exception:
        return None
    for actor in actors:
        try:
            if actor.get_actor_label() == label:
                return actor
        except Exception:
            pass
    return None


def _component_names(components):
    names = []
    for component in components:
        method = getattr(component, "get_name", None)
        if callable(method):
            try:
                names.append(str(method()))
                continue
            except Exception:
                pass
        names.append(_object_path(component))
    return names


def _components_by_class(unreal, actor, class_name):
    component_class = getattr(unreal, class_name, None)
    if not component_class:
        return []
    method = getattr(actor, "get_components_by_class", None)
    if callable(method):
        try:
            return list(method(component_class))
        except Exception:
            return []
    single_method = getattr(actor, "get_component_by_class", None)
    if callable(single_method):
        try:
            component = single_method(component_class)
            return [component] if component else []
        except Exception:
            return []
    return []


def _try_set_property(obj, prop, value):
    method = getattr(obj, "set_editor_property", None)
    if callable(method):
        try:
            method(prop, value)
            return True, ""
        except Exception as exc:
            return False, str(exc)
    return False, "set_editor_property is unavailable"


def _try_call(method, *args):
    if not callable(method):
        return None, "method is unavailable"
    try:
        return method(*args), ""
    except Exception as exc:
        return None, str(exc)


def _try_add_component(unreal, actor, component_class_name, component_label):
    component_class = getattr(unreal, component_class_name, None)
    if not component_class:
        return None, "%s is unavailable" % component_class_name

    transform = None
    transform_class = getattr(unreal, "Transform", None)
    if transform_class:
        try:
            transform = transform_class()
        except Exception:
            transform = None

    attempts = []
    add_by_class = getattr(actor, "add_component_by_class", None)
    if callable(add_by_class):
        if transform is not None:
            attempts.append(lambda: add_by_class(component_class, False, transform, False))
            attempts.append(lambda: add_by_class(component_class, False, transform))
        attempts.append(lambda: add_by_class(component_class))

    add_actor_component = getattr(actor, "add_actor_component", None)
    if callable(add_actor_component):
        attempts.append(lambda: add_actor_component(component_class, component_label))
        attempts.append(lambda: add_actor_component(component_label, component_class))

    last_error = "no component creation API is exposed on this actor"
    for attempt in attempts:
        try:
            component = attempt()
            if component:
                return component, ""
        except Exception as exc:
            last_error = str(exc)
    return None, last_error


def _try_set_relative(component, unreal, location=None, rotation=None):
    if location:
        method = getattr(component, "set_relative_location", None)
        if callable(method):
            try:
                method(unreal.Vector(location[0], location[1], location[2]))
            except Exception:
                pass
    if rotation:
        method = getattr(component, "set_relative_rotation", None)
        if callable(method):
            try:
                method(_rotator(unreal, rotation[0], rotation[1], rotation[2]))
            except Exception:
                pass


def _configure_actor(unreal, actor, class_kind):
    changed = []
    warnings = []

    movement_components = _components_by_class(unreal, actor, "CharacterMovementComponent")
    for movement in movement_components:
        for prop, value in (("max_walk_speed", 600.0), ("jump_z_velocity", 520.0), ("air_control", 0.35)):
            ok, error = _try_set_property(movement, prop, value)
            if ok:
                changed.append({"target": "CharacterMovement.%s" % prop, "value": value})
            else:
                warnings.append({"target": "CharacterMovement.%s" % prop, "warning": error})

    spring_components = _components_by_class(unreal, actor, "SpringArmComponent")
    camera_components = _components_by_class(unreal, actor, "CameraComponent")

    if class_kind == "basic_character_fallback" and not spring_components:
        spring, error = _try_add_component(unreal, actor, "SpringArmComponent", "UEAtelierCameraBoom")
        if spring:
            spring_components.append(spring)
            changed.append({"target": "UEAtelierCameraBoom", "operation": "added_spring_arm_component"})
        else:
            warnings.append({"target": "UEAtelierCameraBoom", "warning": error})

    if class_kind == "basic_character_fallback" and not camera_components:
        camera, error = _try_add_component(unreal, actor, "CameraComponent", "UEAtelierFollowCamera")
        if camera:
            camera_components.append(camera)
            changed.append({"target": "UEAtelierFollowCamera", "operation": "added_camera_component"})
        else:
            warnings.append({"target": "UEAtelierFollowCamera", "warning": error})

    for spring in spring_components:
        ok, error = _try_set_property(spring, "target_arm_length", 360.0)
        if ok:
            changed.append({"target": _object_path(spring), "property": "target_arm_length", "value": 360.0})
        else:
            warnings.append({"target": _object_path(spring), "property": "target_arm_length", "warning": error})
        ok, error = _try_set_property(spring, "use_pawn_control_rotation", True)
        if ok:
            changed.append({"target": _object_path(spring), "property": "use_pawn_control_rotation", "value": True})
        else:
            warnings.append({"target": _object_path(spring), "property": "use_pawn_control_rotation", "warning": error})
        _try_set_relative(spring, unreal, location=(0.0, 0.0, 70.0), rotation=(-10.0, 0.0, 0.0))

    for camera in camera_components:
        ok, error = _try_set_property(camera, "use_pawn_control_rotation", False)
        if ok:
            changed.append({"target": _object_path(camera), "property": "use_pawn_control_rotation", "value": False})
        else:
            warnings.append({"target": _object_path(camera), "property": "use_pawn_control_rotation", "warning": error})
        _try_set_relative(camera, unreal, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0))

    evidence = {
        "characterMovementComponents": _component_names(movement_components),
        "springArmComponents": _component_names(spring_components),
        "cameraComponents": _component_names(camera_components),
        "hasCharacterMovement": bool(movement_components),
        "hasSpringArm": bool(spring_components),
        "hasCamera": bool(camera_components),
    }
    return changed, warnings, evidence


def _set_auto_possess(unreal, actor, enabled):
    auto_receive = getattr(unreal, "AutoReceiveInput", None)
    if not auto_receive:
        return False, "unreal.AutoReceiveInput is unavailable"
    enum_name = "PLAYER0" if enabled else "DISABLED"
    enum_value = getattr(auto_receive, enum_name, None)
    if enum_value is None:
        return False, "AutoReceiveInput.%s is unavailable" % enum_name
    return _try_set_property(actor, "auto_possess_player", enum_value)


def _clear_bare_gamemodebase_override(unreal, world):
    world_settings = None
    try:
        world_settings = world.get_world_settings()
    except Exception:
        return {"changed": False, "reason": "world settings unavailable", "previous": ""}

    current = None
    try:
        current = world_settings.get_editor_property("default_game_mode")
    except Exception:
        current = None
    current_path = _object_path(current)
    if "GameModeBase" not in current_path:
        return {"changed": False, "reason": "existing GameMode override left unchanged", "previous": current_path}

    ok, error = _try_set_property(world_settings, "default_game_mode", None)
    if ok:
        _mark_dirty(world_settings)
        return {"changed": True, "reason": "cleared bare GameModeBase override", "previous": current_path}
    return {"changed": False, "reason": "failed to clear bare GameModeBase override: %s" % error, "previous": current_path}


def _mark_dirty(obj):
    for method_name in ("modify", "mark_package_dirty"):
        method = getattr(obj, method_name, None)
        if callable(method):
            try:
                method()
            except Exception:
                pass
    package = None
    for method_name in ("get_external_package", "get_outermost", "get_package"):
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


def _save_dirty_packages(unreal):
    save_utils = getattr(unreal, "EditorLoadingAndSavingUtils", None)
    save_dirty = getattr(save_utils, "save_dirty_packages", None)
    if callable(save_dirty):
        try:
            return {"attempted": True, "saved": bool(save_dirty(True, True)), "method": "EditorLoadingAndSavingUtils.save_dirty_packages"}
        except Exception as exc:
            return {"attempted": True, "saved": False, "method": "EditorLoadingAndSavingUtils.save_dirty_packages", "error": str(exc)}

    level_subsystem = _get_subsystem(unreal, "LevelEditorSubsystem")
    save_current = getattr(level_subsystem, "save_current_level", None)
    if callable(save_current):
        try:
            return {"attempted": True, "saved": bool(save_current()), "method": "LevelEditorSubsystem.save_current_level"}
        except Exception as exc:
            return {"attempted": True, "saved": False, "method": "LevelEditorSubsystem.save_current_level", "error": str(exc)}

    return {"attempted": False, "saved": False, "method": "", "error": "No non-deprecated map save API is available."}


def _plan(args):
    label = _string(args.get("label"), DEFAULT_LABEL) or DEFAULT_LABEL
    location = _location(args)
    class_path = _string(args.get("classPath"))
    policy = _character_policy(args)
    auto_possess = _bool(args.get("autoPossessPlayer"), True)
    would_write = [
        {"target": "current_editor_level", "operation": "spawn_or_update_actor", "label": label, "location": location},
        {"target": "actor.auto_possess_player", "operation": "set", "value": "PLAYER0" if auto_possess else "DISABLED"},
        {"target": "world_settings.default_game_mode", "operation": "clear_if_bare_GameModeBase"},
        {"target": "character_movement", "operation": "best_effort_configure", "values": {"max_walk_speed": 600.0, "jump_z_velocity": 520.0, "air_control": 0.35}},
        {"target": "camera_or_spring_arm", "operation": "best_effort_configure_or_add_for_basic_fallback", "values": {"target_arm_length": 360.0, "use_pawn_control_rotation": True}},
        {"target": "current_map_package", "operation": "mark_dirty_and_save", "ofpaAware": True},
    ]
    return label, location, class_path, policy, auto_possess, would_write


def execute(args):
    args = args or {}
    dry_run = _bool(args.get("dryRun"), True)
    label, location, requested_class_path, policy, auto_possess, would_write = _plan(args)

    try:
        import unreal
    except Exception as exc:
        return {
            "isError": True,
            "text": "Unreal Python module is not available: %s" % exc,
            "structuredContent": {"action": "create_third_person_ground_character", "dryRun": dry_run},
        }

    character_class, class_path, class_kind, checked_classes, policy = _load_character_class(unreal, args)
    structured = {
        "action": "create_third_person_ground_character",
        "toolName": TOOL_NAME,
        "dryRun": dry_run,
        "label": label,
        "location": location,
        "requestedClassPath": requested_class_path,
        "characterClassPolicy": policy,
        "autoPossessPlayer": auto_possess,
        "candidateClasses": checked_classes,
        "selectedClass": class_path,
        "selectedClassKind": class_kind,
        "gameModeStrategy": "Do not set /Script/Engine.GameModeBase. Clear that bare override only if it is already present, then rely on placed pawn auto possession.",
    }

    if class_kind == "basic_character_fallback":
        structured["fallback"] = {
            "used": True,
            "type": "non_third_person_basic_character",
            "reason": "No known third-person template character was found; spawned /Script/Engine.Character and attempted to add SpringArm and Camera components.",
        }
    elif class_kind == "user_supplied":
        structured["fallback"] = {
            "used": True,
            "type": "user_supplied_unverified_character_class",
            "reason": "A caller-provided classPath was used; this tool does not label it as a built-in third-person template.",
        }
    else:
        structured["fallback"] = {"used": False, "type": "", "reason": ""}

    if dry_run:
        structured["wouldWrite"] = would_write
        structured["notes"] = [
            "Dry run only; no level actors, world settings, or packages were modified.",
            "BP_TwinStickCharacter is not treated as a third-person success by this tool.",
        ]
        return {
            "text": "Dry run: would place or update a PIE-controllable ground character in the current level.",
            "structuredContent": structured,
        }

    if not character_class:
        structured["wouldWrite"] = would_write
        return {"isError": True, "text": "No usable character class could be loaded.", "structuredContent": structured}

    changed = []
    warnings = []
    try:
        world = _get_editor_world(unreal)
        if not world:
            raise RuntimeError("No editor world is active")
        actor_subsystem = _actor_subsystem(unreal)
        if not actor_subsystem:
            raise RuntimeError("EditorActorSubsystem is unavailable")

        structured["mapPackageBefore"] = _package_info(world)
        structured["gameMode"] = _clear_bare_gamemodebase_override(unreal, world)
        if structured["gameMode"].get("changed"):
            changed.append({"target": "world_settings.default_game_mode", "operation": "cleared_bare_GameModeBase"})

        vector = _vector(unreal, location)
        rotator = _rotator(unreal, 0.0, 0.0, 0.0)
        actor = _find_actor_by_label(actor_subsystem, label)
        if actor:
            actor.set_actor_location(vector, False, False)
            actor.set_actor_rotation(rotator, False)
            changed.append({"target": label, "operation": "updated_transform"})
        else:
            actor = actor_subsystem.spawn_actor_from_class(character_class, vector, rotator)
            if not actor:
                raise RuntimeError("EditorActorSubsystem failed to spawn '%s'." % class_path)
            actor.set_actor_label(label)
            changed.append({"target": label, "operation": "spawned", "class": class_path})

        ok, error = _set_auto_possess(unreal, actor, auto_possess)
        if ok:
            changed.append({"target": label + ".auto_possess_player", "value": "PLAYER0" if auto_possess else "DISABLED"})
        else:
            warnings.append({"target": label + ".auto_possess_player", "warning": error})

        actor_changes, actor_warnings, evidence = _configure_actor(unreal, actor, class_kind)
        changed.extend(actor_changes)
        warnings.extend(actor_warnings)

        _mark_dirty(actor)
        _mark_dirty(world)
        save_result = _save_dirty_packages(unreal)

        structured["changed"] = changed
        structured["warnings"] = warnings
        structured["actorPath"] = _object_path(actor)
        structured["actorPackage"] = _package_info(actor)
        structured["externalActorPackage"] = _external_package_info(actor)
        structured["mapPackageAfter"] = _package_info(world)
        structured["save"] = save_result
        structured["componentEvidence"] = evidence
        structured["thirdPersonReadiness"] = {
            "hasMovement": evidence["hasCharacterMovement"],
            "hasCamera": evidence["hasCamera"],
            "hasSpringArm": evidence["hasSpringArm"],
            "isKnownThirdPersonTemplate": class_kind == "third_person_candidate",
            "isNonThirdPersonFallback": class_kind in ("basic_character_fallback", "user_supplied"),
        }
        if class_kind != "third_person_candidate":
            structured["notes"] = [
                "This result is reported as a fallback, not as proof that a real third-person template character exists.",
            ]
        return {"text": "Placed or updated PIE-controllable character '%s'." % label, "structuredContent": structured}
    except Exception as exc:
        structured["changed"] = changed
        structured["warnings"] = warnings
        structured["wouldWrite"] = would_write
        return {
            "isError": True,
            "text": "Failed to place third-person ground character: %s" % exc,
            "structuredContent": structured,
        }
