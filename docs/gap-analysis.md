# Unreal Engine MCP Gap Analysis

> Generated 2026-03-06. Current state: 35 tools, 600+ actions across 6 categories.

## TIER 1: Critical Gaps

### 1. ~~Data Tables / Curve Tables / Composite Tables~~ DONE
- **UE5 API:** `UDataTable`, `UCurveTable`, `UCompositeDataTable`, `DataTableFunctionLibrary`
- **Status:** `manage_data_table` tool with 9 actions (create, list_rows, get_row, add_row, edit_row, remove_row, get_structure, import_json, export_json). Curve/Composite tables remain uncovered.

### 2. PCG (Procedural Content Generation) Framework
- **UE5 API:** `UPCGComponent`, `UPCGGraph`, PCG Editor Mode (5.7), custom nodes via `UPCGSettings`
- **Status:** Roadmap Phase 27, no actions exist
- **Why:** Flagship UE5 system for procedural world population. LLM creating PCG graphs = transformative.

### 3. ~~Gameplay Tags~~ DONE
- **UE5 API:** `UGameplayTagsManager`, `FGameplayTag`, `FGameplayTagContainer`
- **Status:** `manage_gameplay_tags` tool with 9 actions (add_tag, remove_tag, list_tags, get_tag_children, has_tag, add_tag_to_actor, remove_tag_from_actor, get_actor_tags, get_tag_hierarchy).

### 4. ~~Data Assets / Primary Assets~~ DONE
- **UE5 API:** `UDataAsset`, `UPrimaryDataAsset`, `UAssetManager`, Asset Bundles
- **Status:** `manage_data_asset` tool with 6 actions (create_data_asset, create_data_asset_blueprint, get/set_data_asset_properties, list_data_assets, duplicate_data_asset). Asset Bundles/AssetManager remain uncovered.

### 5. Movie Render Queue
- **UE5 API:** `UMoviePipelineQueueSubsystem`, batch jobs, multi-format output (EXR, ProRes)
- **Status:** Zero coverage
- **Why:** Cinematics, trailers, archviz. "Render this sequence at 4K with path tracing."

### 6. ~~String Tables / Localization~~ DONE
- **UE5 API:** `FStringTable`, localization pipeline, .po files, culture/locale
- **Status:** `manage_string_table` tool with 9 actions (create_string_table, add_entry, remove_entry, edit_entry, get_entry, list_entries, import_json, export_json, list_string_tables). Localization pipeline (.po files, culture/locale) remains uncovered.

### 7. ~~Undo Transaction Management~~ DONE
- **UE5 API:** `FScopedTransaction`, `GEditor->BeginTransaction/EndTransaction`
- **Status:** All MCP handler dispatches now wrapped in `FScopedTransaction` in ProcessAutomationRequest. Operations appear in Edit > Undo as "MCP: <action>". Handlers that call `Modify()` before changes get proper undo support.

## TIER 2: Important Gaps

### 8. Chaos Destruction System
- **UE5 API:** Geometry Collections, Voronoi/planar/cluster fracture, physics fields, Fracture Mode
- **Status:** Documented as NOT_IMPLEMENTED

### 9. Water System
- **UE5 API:** Water bodies (ocean, lake, river), buoyancy, fluid sim, shoreline effects
- **Status:** Zero coverage

### 10. Post Process Volumes
- **UE5 API:** `APostProcessVolume` — exposure, bloom, DOF, color grading, tone mapping
- **Status:** Zero direct coverage. Most common "make it look right" operation.

### 11. ~~Animation Notifies~~ DONE
- **UE5 API:** `UAnimNotify`, `UAnimNotifyState` — footsteps, attack windows, VFX triggers
- **Status:** `manage_anim_notify` tool with 6 actions (add_notify, add_notify_state, remove_notify, list_notifies, set_notify_properties, list_notify_classes).

### 12. Control Rig (deeper)
- **UE5 API:** Full node graph, FK/IK, physics sim, procedural animation
- **Status:** Basic scaffolding (`create_control_rig`, `add_control`). No Sequencer track.

### 13. Enhanced Input (deeper)
- **UE5 API:** Triggers (tap, hold, pulse, chord), Modifiers (dead zones, scalar, FOV scaling)
- **Status:** Basic create/map. No trigger/modifier configuration.

### 14. Common UI Framework
- **UE5 API:** `UCommonActivatableWidget`, gamepad nav, input routing, action bars
- **Status:** Widget authoring has no Common UI awareness.

### 15. Source Control Operations (deeper)
- **UE5 API:** `USourceControlModule` — checkout, check-in, revert, diff, history, shelving
- **Status:** Only `source_control_checkout` and `source_control_submit` for individual assets.

### 16. Cooking / Packaging / Build Pipeline
- **UE5 API:** UAT `BuildCookRun`, commandlets, platform deploy
- **Status:** `run_ubt` covers compile only. No cook, package, or deploy.

### 17. Modeling Mode Tools
- **UE5 API:** PolyEdit, mesh booleans, UV editing, retopology (interactive editor tools)
- **Status:** `manage_geometry` has Geometry Script primitives. Editor Modeling Mode not exposed.

### 18. ~~Actor Layer Management~~ DONE
- **UE5 API:** `ULayersSubsystem` — create/delete/rename layers, add/remove actors, toggle visibility
- **Status:** `manage_layers` tool with actions for layer CRUD and actor assignment.

### 19. ~~Physics Materials~~ DONE
- **UE5 API:** `UPhysicalMaterial` — surface types, friction, restitution, impact sounds
- **Status:** `manage_physics_material` tool with actions for create, configure, list, assign, and duplicate.

### 20. ~~Blueprint Interfaces~~ DONE
- **UE5 API:** `UBlueprintInterfaceFactory`
- **Status:** `manage_blueprint_interface` tool with actions for create, add/remove functions, list, implement on blueprint.

## TIER 3: Valuable Gaps

### 21. Pixel Streaming Control
- **UE5 API:** WebRTC streaming, signaling server, input routing
- **Status:** Zero

### 22. Virtual Production / VCam
- **UE5 API:** Virtual camera, Live Link, LED wall calibration
- **Status:** Zero

### 23. MetaSounds (deeper graph)
- **UE5 API:** Full DSP node graph, oscillators, filters, envelopes
- **Status:** Basic `create_metasound`, `add_metasound_node`. Thin C++ depth.

### 24. Mass Entity / Mass Gameplay
- **UE5 API:** ECS framework, MassSpawner, MassTraits, MassProcessors
- **Status:** Basic scaffolding in `manage_ai`.

### 25. Online Subsystem / Platform Services
- **UE5 API:** Steam, EOS, Xbox Live — matchmaking, leaderboards, achievements
- **Status:** `manage_sessions` and `manage_networking` exist, scope unclear.

### 26. Automation Testing Framework
- **UE5 API:** `FAutomationTestBase`, functional tests, screenshot comparison
- **Status:** `run_tests` exists. No test creation or functional test actors.

### 27. Sequencer Track Types (deeper)
- **Status:** Only float, transform, camera, skeletal anim tracks
- **Missing:** Audio tracks, event/callable tracks, control rig tracks, morph target tracks, fade tracks

### 28. Asset Validation
- **UE5 API:** `UEditorValidatorSubsystem` — register on-save validation rules
- **Status:** Zero

### 29. Custom Collision Profiles
- **UE5 API:** `UCollisionProfile` — create new collision presets
- **Status:** Zero

### 30. Interchange Framework
- **UE5 API:** UE 5.1+ format-agnostic import (USD/glTF/FBX/Alembic) with pipeline config
- **Status:** Basic import exists, no pipeline configuration.

## TIER 4: Nice-to-Have

| Gap | UE5 System |
|-----|-----------|
| Hair/Groom | Groom system, Alembic |
| MetaHuman framework | MetaHuman |
| Texture/Material Baking | Bake to texture |
| Landscape Spline Roads | Landscape splines for roads |
| Custom Struct/Enum creation | Runtime type creation |
| Editor Utility Widgets | EUW for custom editor panels |
| Commandlets | Custom batch processing |
| Horde Build System | Epic's CI/CD |
| nDisplay | Multi-display rendering |
| DMX | Stage lighting control |
| Actor Grouping | `UActorGroupingUtils` |
| Actor Type Conversion | StaticMesh <-> Blueprint <-> Volume |
| Asset Consolidation | Merge duplicate assets |
| Content Browser Navigation | Programmatic queries |
| Editor Notifications | Toasts/dialogs |
| Quartz Audio | Beat-synced audio |
| Audio Bus/Submix Routing | Signal routing |
| Root Motion Config | Extraction settings on AnimSequence |
| Virtual Shadow Maps | `r.Shadow.Virtual.*` CVars |
| Blueprint Diff/Merge | `FBlueprintEditorUtils` |

## Existing Coverage Needing Depth

| Area | Current | Missing |
|------|---------|---------|
| Sequencer | Float, transform, camera, skeletal anim | Audio, event, control rig, morph, fade tracks |
| Blueprint Graphs | Node CRUD, pin connections | Macros, collapsed nodes, debugging, auto-layout |
| Niagara Graphs | Module add/remove, basic params | Full graph editing, param collections, GPU sim, LOD |
| Widget Authoring | Widget tree CRUD | Event binding, delegates, styling, drag-drop, Common UI |
| Material Graphs | Expression CRUD, connections | Material Functions, material layers, param collections |
| Object Properties | Set/get with special actor props | Nested struct access, array indexing, enum validation |
| SCS Components | Add/remove/transform/property | Batch ops, undo integration, attachment constraints |
| Game Framework | Blueprint scaffolding | Match states, teams, scoring, session management |

## Implementation Priority

### Immediate (highest LLM value) — COMPLETE
1. ~~Data Tables~~ DONE
2. ~~Gameplay Tags~~ DONE
3. ~~Data Assets~~ DONE
4. ~~Undo transactions~~ DONE

### Short-term — COMPLETE
5. ~~String Tables~~ DONE
6. ~~Animation Notifies~~ DONE
7. ~~Actor Layer Management~~ DONE
8. ~~Blueprint Interfaces~~ DONE
9. ~~Physics Materials~~ DONE

### Short-term — NEXT
10. Post Process Volumes (create, configure exposure/bloom/DOF/color grading)
11. PCG Framework (execute graph, set parameters, trigger generation)
12. Movie Render Queue (queue sequences, configure output, render)
13. Chaos Destruction (create geometry collection, fracture, configure)
14. Water System (create water bodies, configure buoyancy)

### Medium-term
15. Sequencer track types (audio, event, control rig)
16. Common UI framework support
17. Source control depth (revert, history, changelists)
18. Enhanced Input triggers/modifiers
19. Cooking/packaging via UAT

### Long-term
20. Pixel Streaming / Virtual Production
21. Mass Entity deeper coverage
22. MetaSounds graph depth
23. Online Subsystem / platform services
24. Editor Utility Widgets
25. Modeling Mode tools
