#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "format/mdl.hpp"
#include "modeldata.hpp"
#include "utils/mathlib.hpp"

class QC
{
public:
    std::string modelname;                                  // $modelname
    std::filesystem::path cd;                               // $cd
    std::filesystem::path cdtexture;                        // $cdtexture
    Vector3 eyeposition{};                                  // $eyeposition
    float scale = 1.0f;                                     // $scale
    float scale_body_and_sequence = 1.0f;                   // $body studio <value> // also for $sequence
    Vector3 origin{};                                       // $origin
    float origin_rotation = to_radians(ENGINE_ORIENTATION); // $origin <X> <Y> <Z> <rotation>
    float rotate = 0.0f;                                    // $rotate and $sequence <sequence name> <SMD path> {[rotate <zrotation>]} only z axis
    Vector3 sequence_origin{};                              // $sequence <sequence name> <SMD path> {[origin <X> <Y> <Z>]}
    float gamma = 1.8f;                                     // $$gamma

    std::vector<RenameBone> renamebones;             // $renamebone
    std::vector<HitGroup> hitgroups;                 // $hgroup
    std::vector<std::string> mirroredbones;          // $mirrorbone

    std::array<std::array<int, 32>, 32> texturegroups{}; // $texturegroup
    int texturegroup_rows = 0;
    int texturegroup_cols = 0;

    std::array<Vector3, 2> bbox{}; // $bbox
    std::array<Vector3, 2> cbox{}; // $cbox

    std::vector<HitBox> hitboxes;                // $hbox
    std::vector<BoneController> bonecontrollers; // $controller
    std::vector<Attachment> attachments;         // $attachment

    std::vector<Sequence> sequences; // $sequence

    std::vector<std::unique_ptr<Model>> submodels; // $body

    std::vector<BodyPart> bodyparts; // $bodygroup

    int flags = 0; // $flags

    // Constructor
    QC()
    {
        // Initialize arrays with default values
        texturegroups.fill({});
        bbox.fill(Vector3{});
        cbox.fill(Vector3{});

        renamebones.reserve(MAXSTUDIOSRCBONES);
        hitgroups.reserve(MAXSTUDIOSRCBONES);
        mirroredbones.reserve(MAXSTUDIOSRCBONES);
        hitboxes.reserve(MAXSTUDIOSRCBONES);
        bonecontrollers.reserve(MAXSTUDIOSRCBONES);
        attachments.reserve(MAXSTUDIOSRCBONES);

        sequences.reserve(MAXSTUDIOSEQUENCES);

        submodels.reserve(MAXSTUDIOMODELS);
        bodyparts.reserve(MAXSTUDIOBODYPARTS);
    }
};

// Splits QC script text into whitespace-separated tokens; ';', '#' and '//' start comments.
class QcTokenizer
{
public:
    void load(std::string_view text);

    bool get_token(bool crossline, std::string &token);
    bool token_available() const;
    bool end_of_file() const { return end_of_qc_file; }
    int line_number() const { return qc_line_number; }

private:
    std::vector<char> qc_script_buffer;
    char *qc_stream_p = nullptr;
    char *qc_stream_end_p = nullptr;
    bool token_ready = false;
    bool end_of_qc_file = false;
    int qc_line_number = 0;
};
