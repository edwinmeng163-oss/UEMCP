# Bad Patch Scaffold Fixture

This fixture intentionally contains a truncated `CategoryHandlerFunction.patch.cpp`.
It is used to verify that scaffold inspection and apply dry-runs refuse incomplete
AI-generated code before it reaches source files.

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
