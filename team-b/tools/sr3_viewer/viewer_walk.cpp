#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_walk_session.h"

using namespace sr3viewer;


int runWalkDemo(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: sr3_viewer walk <characters.vpp_pc> <misc_tables.vpp_pc> "
                     "<sr3_city_0.vpp_pc> [<more city archives>...]\n"
                     "                       [--eye X Y Z] [--yaw D] [--pitch D] [--no-input]\n"
                     "                       [--frames N] [--capture <out.png>] [--capture-frame N]\n"
                     "                       [--capture-frames F0,F1,... ] [--v2]\n"
                     "                       [--size W H] [--tint] [--no-textures] [--no-outlines]\n"
                     "                       [--speed U] [--far F]\n"
                     "       Drives the real player<->animation bridge (sr3playeranim) through a\n"
                     "       SCRIPTED on-foot sequence (40 frames forward, 20 'turning' [see the\n"
                     "       command's own doc comment for the honest caveat], 20 stopped) on the\n"
                     "       real city_corner ground, with the real follow camera's target kept in\n"
                     "       sync every frame (sr3playeranim::DriveCameraTarget). Default camera\n"
                     "       framing matches tests/golden/city_corner's own frozen framing.\n"
                     "       Character render is a RIGID BODY (one bind-pose skin, moved/rotated as\n"
                     "       a whole) - this project has no real per-clip skeletal animation\n"
                     "       sampling/skinning pipeline. See this command's own doc comment for the\n"
                     "       full REAL/CHOSEN/judgement-call breakdown.\n"
                     "       --v2 (walk_demo v2, see runWalkDemo()'s own 'V2' doc block below):\n"
                     "       brad's real per-material diffuse textures are resolved and bound\n"
                     "       (drawTextured, same sr3geometry::MaterialBindings/findAndUploadTexture\n"
                     "       path runMesh()'s own --mode textured uses) instead of the flat Checker\n"
                     "       pattern; the real sr3player::CameraSystem follow camera is driven\n"
                     "       through its OWN preUpdate()/update() pipeline every frame (camera_free.xtbl\n"
                     "       seeded from misc_tables.vpp_pc) and the capture's view comes from its\n"
                     "       real COMMITTED eye/rows/FOV (spec-player-runtime.md Sec4.10), not a\n"
                     "       hand-picked --eye/--yaw/--pitch; and the city streaming focus is brad's\n"
                     "       own live position (not the free-fly debug eye), so cells around wherever\n"
                     "       the scripted walk actually puts him stay loaded. --eye/--yaw/--pitch are\n"
                     "       ignored for the render view under --v2 (still used, unmodified, for the\n"
                     "       focus-box outline's placement and when --v2 is absent). Without --v2 this\n"
                     "       command's behaviour (including tests/golden/walk_demo's own frozen\n"
                     "       baseline) is BYTE-FOR-BYTE UNCHANGED.\n"
                     "       --capture-frames F0,F1,...: captures several frames instead of one, each\n"
                     "       written to '<--capture path>_frameN.png'; --capture-frame/--capture alone\n"
                     "       (no --capture-frames) keeps the original single-file behaviour exactly.\n"
                     "       FIX 4 (--v2 only, real U4 pose compose): loads real plym_ps_run/jog/wlk\n"
                     "       locomotion clips from preload_anim.vpp_pc (derived from the characters.vpp_pc\n"
                     "       path's own directory by default; --anim-archive <path> overrides it) and\n"
                     "       re-skins/re-uploads brad's mesh every frame with the real composed U4 pose\n"
                     "       (sr3animpose) instead of the rigid bind-pose render. Falls back to the rigid\n"
                     "       render, unchanged, if the archive or clips are not found.\n"
                     "       SR3_ANIM_DEBUG_LOG=1 (env var, unset/0 = silent, default): per CAPTURED frame\n"
                     "       only, dumps the real node-graph output list (clip/tag names, weights,\n"
                     "       transition flags) and the post-step T/E/playback-rate to stderr - see\n"
                     "       LogWalkDemoAnimDiagnostics()'s own doc comment above runWalkDemo().\n"
                     "       --head-matlib-attach (--v2 only, OFF BY DEFAULT, DIAGNOSTIC-ONLY): resolves\n"
                     "       and attaches brad's brad_head.matlib_pc as its own identity-keyed,\n"
                     "       type-0x16 resource (spec-tables-customization.md Sec12.1a/12.1b's real,\n"
                     "       confirmed matlib-consumption mechanism - compose filename -> exists-check\n"
                     "       -> attach as a named slot keyed by the character's own identity), and logs\n"
                     "       it. Does NOT bind any of the matlib's textures to any material slot. Never\n"
                     "       changes rendered output; never affects any frozen golden baseline.\n"
                     "       --head-matlib-bind (--v2 only, OFF BY DEFAULT, implies --head-matlib-attach):\n"
                     "       spec-tables-customization.md Sec12.1f closes NPC skinning as individually-\n"
                     "       named, pre-baked per-part textures named by the character's own matlib - but\n"
                     "       neither Sec12.1c nor Sec12.1f states a confirmed rule for WHICH matlib entry\n"
                     "       replaces WHICH mesh material slot (Sec12.1c: the matlib's own texture-name\n"
                     "       table is 'explicitly skipped and never retained' by the real engine at\n"
                     "       runtime - no traced instruction matches an entry to a slot by name, hash, or\n"
                     "       position). This flag therefore applies a CHOSEN, NOT spec-confirmed,\n"
                     "       role-suffix heuristic (sr3geometry::PickMatlibTextureByRoleSuffix(), see its\n"
                     "       own doc comment in include/sr3geometry/character_matlib.h for the full\n"
                     "       citation) that REPLACES the head mesh's own placeholder diffuse/normal\n"
                     "       TextureRef names (missing.tga / flat-normalmap_n.tga) with the matlib's own\n"
                     "       resolved real names (brad_sm_head_d.tga / brad_sm_head_n.tga), reusing the\n"
                     "       SAME by-param-hash role identification material_binding.h already uses\n"
                     "       project-wide (kParamHashDiffuseA/B, kParamHashNormalMap) - not a parallel\n"
                     "       mechanism. CHANGES rendered output (the head's diffuse texture) when passed.\n"
                     "       OFF BY DEFAULT; no existing golden baseline passes this flag, so none is\n"
                     "       affected; deliberately NOT promoted to default and NOT frozen as a new\n"
                     "       golden here, since its role-matching rule is a judgement call, not a\n"
                     "       confirmed engine mechanism - that is this flag's own owning session's call,\n"
                     "       pending visual sign-off and/or a Team-A spec-sync answer on the real rule.\n"
                     "       Supersedes the prior --experimental-head-matlib texture-NAME-swap heuristic\n"
                     "       the SAME way --head-matlib-attach already did (same caveats apply: still a\n"
                     "       heuristic, now CHOSEN deliberately and labelled as such instead of assumed).\n"
                     "       --collision (2026-10-07 character-proxy/collision follow-up task, a SEPARATE\n"
                     "       opt-in from --v2): wires a real sr3player::CharacterProxyDriver into\n"
                     "       PlayerMovement::collisionHook so brad's capsule is actually resolved against\n"
                     "       REAL .clmesh_pc collision triangles (ClmeshTriangleCollisionWorld, 2026-10-08 -\n"
                     "       see runWalkDemo()'s own doc comment above that class for the real-data source,\n"
                     "       the CHOSEN own-BVH acceleration structure, and the honest scope caveats -\n"
                     "       street-furniture/wall props only, not the city street/building-shell surface,\n"
                     "       and props still sit at a FAKE per-cell grid position, same as before) instead\n"
                     "       of passing through walls. Supersedes the prior render-mesh-AABB stand-in\n"
                     "       (CityPropAabbCollisionWorld, kept defined in this file for reference/history,\n"
                     "       no longer constructed) the SAME way --head-matlib-bind supersedes the prior\n"
                     "       --experimental-head-matlib heuristic above. 2026-10-08 castRay/shape-cast\n"
                     "       follow-up: with --v2 also passed, --collision ALSO wires a real\n"
                     "       RealCameraCollision (see its own doc comment above) into the follow camera's\n"
                     "       sr3player::CameraCollision seam, so the camera itself now sweeps a real 3D\n"
                     "       sphere against the SAME real .clmesh_pc triangles instead of the prior\n"
                     "       sr3player::NoHitCameraCollision no-collision stand-in - the camera no longer\n"
                     "       clips through a real wall the character proxy already stops at. Without\n"
                     "       --collision (or without --v2) this command's behaviour (including both frozen\n"
                     "       walk_demo/walk_demo_v2 baselines) is BYTE-FOR-BYTE UNCHANGED.\n"
                     "       --walk-forward-frames N / --walk-turn-frames N: override the scripted\n"
                     "       sequence's 40-forward/20-turn phase lengths (default 40/20, matching every\n"
                     "       existing golden exactly) - only useful paired with --collision, to walk far\n"
                     "       enough to actually reach a wall within the capture window; the 'stopped'\n"
                     "       phase still fills every remaining frame up to --frames.\n"
                     "       --visible: shows the render window instead of the default hidden one\n"
                     "       (off by default - see sr3render/window.h's own doc comment).\n");
        return 1;
    }
    WalkSession sess;
    if (int rc = walkSetupArgs(sess, argc, argv); rc != -1) return rc;
    if (int rc = walkSetupBrad(sess); rc != -1) return rc;
    if (int rc = walkSetupAnimRuntime(sess); rc != -1) return rc;
    if (int rc = walkSetupPlayerAndCamera(sess); rc != -1) return rc;
    if (int rc = walkSetupCityArchives(sess); rc != -1) return rc;
    if (int rc = walkSetupWindowDevice(sess); rc != -1) return rc;
    if (int rc = walkSetupCharacterTextures(sess); rc != -1) return rc;
    if (int rc = walkSetupCharacterHead(sess); rc != -1) return rc;
    if (int rc = walkSetupStreaming(sess); rc != -1) return rc;
    if (int rc = walkSetupLoopState(sess); rc != -1) return rc;

    auto& window = *sess.window_;
    auto& swapChain = *sess.swapChain_;
    auto& focusBox = *sess.focusBox_;
    auto& device = *sess.device_;
    auto& pm = *sess.pm_;
    auto& bridge = *sess.bridge_;
    auto& error = sess.error;
    auto& depthWidth = sess.depthWidth;
    auto& depthHeight = sess.depthHeight;
    auto& frames = sess.frames;
    auto& maxFrames = sess.maxFrames;
    while (window.pumpMessages()) {
        if (!swapChain.resize(window.width(), window.height(), error)) break;
        if (swapChain.width() != depthWidth || swapChain.height() != depthHeight) {
            if (focusBox.createDepth(device.device(), swapChain.width(), swapChain.height(), error)) {
                depthWidth = swapChain.width();
                depthHeight = swapChain.height();
            }
        }
        walkFrameInput(sess);
        WalkFrame frame;
        walkFramePlayer(sess, frame);
        walkFrameRender(sess, frame);
        walkFrameCapture(sess, frame);

        swapChain.present(true);
        ++frames;
        if (maxFrames >= 0 && frames >= maxFrames) break;
    }
    std::printf("frames   : %d presented; final player pos (%.2f %.2f %.2f), locomotion state %d, "
                "playback rate %.3f\n",
                frames, pm.st.position.x, pm.st.position.y, pm.st.position.z, pm.st.locomotionState,
                bridge.playbackRate());
    return 0;
}
