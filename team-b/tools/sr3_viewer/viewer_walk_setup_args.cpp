// runWalkDemo() split: command-line parsing and the banner printout.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupArgs(WalkSession& sess, int argc, char** argv) {
    auto& charactersPath = sess.charactersPath;
    auto& miscTablesPath = sess.miscTablesPath;
    auto& cityArchivePaths = sess.cityArchivePaths;
    auto& noInput = sess.noInput;
    auto& tintMode = sess.tintMode;
    auto& textures = sess.textures;
    auto& outlines = sess.outlines;
    auto& winWidth = sess.winWidth;
    auto& winHeight = sess.winHeight;
    auto& maxFrames = sess.maxFrames;
    auto& captureFrame = sess.captureFrame;
    auto& capturePath = sess.capturePath;
    auto& captureFrames = sess.captureFrames;
    auto& v2 = sess.v2;
    auto& collisionEnabled = sess.collisionEnabled;
    auto& headMatlibAttach = sess.headMatlibAttach;
    auto& headMatlibBind = sess.headMatlibBind;
    auto& walkForwardFrames = sess.walkForwardFrames;
    auto& walkTurnFrames = sess.walkTurnFrames;
    auto& animArchiveOverride = sess.animArchiveOverride;
    auto& eye = sess.eye;
    auto& yaw = sess.yaw;
    auto& pitch = sess.pitch;
    auto& moveSpeed = sess.moveSpeed;
    auto& farZ = sess.farZ;
    auto& visible = sess.visible;
    charactersPath = argv[2];
    miscTablesPath = argv[3];
    int argi = 4;
    while (argi < argc && std::strncmp(argv[argi], "--", 2) != 0) cityArchivePaths.push_back(argv[argi++]);
    if (cityArchivePaths.empty()) {
        std::fprintf(stderr, "need at least one city archive (sr3_city_0.vpp_pc)\n");
        return 2;
    }
    for (; argi < argc; ++argi) {
        const std::string arg = argv[argi];
        if (arg == "--no-input") noInput = true;
        else if (arg == "--collision") collisionEnabled = true;
        else if (arg == "--walk-forward-frames" && argi + 1 < argc) walkForwardFrames = std::atoi(argv[++argi]);
        else if (arg == "--walk-turn-frames" && argi + 1 < argc) walkTurnFrames = std::atoi(argv[++argi]);
        else if (arg == "--tint") tintMode = true;
        else if (arg == "--no-textures") textures = false;
        else if (arg == "--no-outlines") outlines = false;
        else if (arg == "--v2") v2 = true;
        else if (arg == "--head-matlib-attach") headMatlibAttach = true;
        else if (arg == "--head-matlib-bind") { headMatlibAttach = true; headMatlibBind = true; }
        else if (arg == "--anim-archive" && argi + 1 < argc) animArchiveOverride = argv[++argi];
        else if (arg == "--size" && argi + 2 < argc) {
            winWidth = static_cast<uint32_t>(std::atoi(argv[++argi]));
            winHeight = static_cast<uint32_t>(std::atoi(argv[++argi]));
        } else if (arg == "--frames" && argi + 1 < argc) {
            maxFrames = std::atoi(argv[++argi]);
        } else if (arg == "--capture" && argi + 1 < argc) {
            capturePath = argv[++argi];
        } else if (arg == "--capture-frame" && argi + 1 < argc) {
            captureFrame = std::atoi(argv[++argi]);
        } else if (arg == "--capture-frames" && argi + 1 < argc) {
            const std::string csv = argv[++argi];
            size_t pos = 0;
            while (pos < csv.size()) {
                size_t comma = csv.find(',', pos);
                const std::string tok = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty()) captureFrames.push_back(std::atoi(tok.c_str()));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        } else if (arg == "--eye" && argi + 3 < argc) {
            eye[0] = static_cast<float>(std::atof(argv[++argi]));
            eye[1] = static_cast<float>(std::atof(argv[++argi]));
            eye[2] = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--yaw" && argi + 1 < argc) {
            yaw = static_cast<float>(std::atof(argv[++argi])) * 3.14159265f / 180.0f;
        } else if (arg == "--pitch" && argi + 1 < argc) {
            pitch = static_cast<float>(std::atof(argv[++argi])) * 3.14159265f / 180.0f;
        } else if (arg == "--speed" && argi + 1 < argc) {
            moveSpeed = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--far" && argi + 1 < argc) {
            farZ = static_cast<float>(std::atof(argv[++argi]));
        } else if (arg == "--visible") {
            visible = true;
        } else {
            std::fprintf(stderr, "unrecognised argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::printf("=== walk: player<->animation bridge demo (sr3playeranim) ===\n");
    std::printf("REAL   : the 'player state machine' node graph (misc_tables.vpp_pc), driven every\n"
                "         frame through sr3playeranim::PlayerAnimBridge::stepFrame(); real city_corner\n"
                "         ground cells + textures (same streaming as `city`); brad's real rigged mesh;\n"
                "         the follow camera's real target hookup (DriveCameraTarget, every frame).\n");
    std::printf("CHOSEN : scripted input (40 forward / 20 'turning' [see note below] / 20 stopped);\n"
                "         character start (-960,0,-700) - the city_corner golden's own named focus\n"
                "         corner, y=0 matching every streamed prop's own fake-grid placement; constant\n"
                "         synthetic root-motion rate (no real per-clip root motion anywhere in this\n"
                "         project); rigid-body render (one bind-pose skin moved/rotated as a whole -\n"
                "         no real per-clip bone rotation data exists here either, so no fake leg\n"
                "         motion is invented); the follow camera's OWN submode/collision pipeline is\n"
                "         NOT run (its target is, see above) - the capture still uses the SAME fixed\n"
                "         debug framing tests/golden/city_corner already froze.\n");
    std::printf("NOTE   : 'turning' is NOT a true turn-in-place - this movement model has no separate\n"
                "         yaw stick; engaging the A axis alone STRAFES the character sideways while\n"
                "         facingA50 (= unit(desiredMove)) swings to match the new movement direction,\n"
                "         so it reads as a turn+step, not a pure yaw. Said plainly, not hidden.\n");
    return -1;
}

}  // namespace sr3viewer
