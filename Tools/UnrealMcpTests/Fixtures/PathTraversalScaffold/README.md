# Path Traversal Scaffold Fixture

This fixture contains a malicious `ScaffoldMetadata.json` where
`categorySourceFile` escapes the plugin source tree.
It is used to verify that apply dry-runs reject path traversal before any
source file outside `Plugins/UnrealMcp/Source` is touched.

## Requested Argument Schema

```json
{
  "type": "object",
  "properties": {
    "message": {
      "type": "string",
      "description": "Fixture message."
    }
  },
  "additionalProperties": false
}
```
