# Auto Decorator (Geode mod for Geometry Dash 2.2081)

Targets **Geode v5.11.2**, GD 2.2081. Build: `geode build` (needs `GEODE_SDK`).

## Layout
- `core/`  – SDK-independent engine (plain C++17): grid analysis, structure/hole/corner detection, shape
  classification (rectangle / triangle / circle / complex), procedural generator, variations, palettes, optimiser.
  **Compiled and tested here** (`tools/test_main.cpp` + `tools/render.py` render the result to PNG).
- `src/`   – Geode glue: object catalog, editor scan, preview/apply/undo, 9-tab UI, keybinds.
  **Written against the real 2.2081 bindings, but NOT compiled and NOT run in Geometry Dash yet.**

## Honest status / things to verify on first build
1. Object IDs are discovered at runtime (decoration-type objects, matched by frame-name keywords). Check the
   *Decoration Library* tab for what each role resolved to; override via mod settings `ids-<role>` if wrong.
2. `UndoCommand::Paste` is used so one Ctrl+Z removes a whole batch – verify in the editor.
3. Z-layer mapping (`zLayerFor` in `Editor.cpp`) and writing palette colours into channels are unverified.
4. Slopes and rotated blocks are rasterised to full 30-unit cells (limitation of the grid analysis).
5. Preview creates the real objects; Cancel removes them. Close the game while previewing and they stay.
