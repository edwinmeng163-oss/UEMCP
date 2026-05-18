# Material Instance Tools

The v0.16 material surface is intentionally small and type-safe:

1. `unreal.material_instance_list` finds Material Instance assets under a content root.
2. `unreal.material_instance_get_parameters` reads scalar, vector, texture, and static switch parameters.
3. `unreal.material_instance_set_scalar` writes one numeric scalar parameter.
4. `unreal.material_instance_set_vector` writes one RGBA vector parameter.

Use the tools in that order for requests like "make this material redder":
list likely instances, inspect parameters, choose a color/tint vector parameter,
then set it with a JSON object shaped `{ "r": 1.0, "g": 0.2, "b": 0.15, "a": 1.0 }`.

Setters only edit `UMaterialInstanceConstant` assets and reject wrong value
types with `errorKind: "TypeMismatch"`. Texture and static switch setters are
deferred because they need asset-reference validation and static permutation
handling beyond this first material instance surface.
