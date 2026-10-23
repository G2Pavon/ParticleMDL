// studiomdl.cpp: generates a studio .mdl file from a .qc script

#include "studiomdl.hpp"
#include "studiomdl/studiomdl.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "format/image/bmp.hpp"
#include "format/mdl.hpp"
#include "format/qc.hpp"
#include "monsters/activity.hpp"
#include "monsters/activitymap.hpp"
#include "utils/cmdlib.hpp"
#include "utils/linereader.hpp"
#include "utils/linescanner.hpp"
#include "utils/mathlib.hpp"
#include "writemdl.hpp"

namespace
{
class QcCompiler : public CompileState
{
public:
	QcCompiler(const studiomdl::FileSystem &fs, const studiomdl::CompileOptions &options);

	void compile_text(std::string_view qc_path, std::string_view qc_text);

private:
	void extract_motion();
	void optimize_animations();
	int find_node(const std::string &name);
	void make_transitions();
	void simplify_model();
	int find_texture_index(const std::string &texturename);
	Mesh *find_mesh_by_texture(Model *pmodel, const std::string &texturename);
	int find_vertex_normal_index(Model *pmodel, const Normal *pnormal);
	int find_vertex_index(Model *pmodel, Vertex *pv);
	void resize_texture(Texture *ptexture);
	void grab_skin(Texture *ptexture);
	void set_skin_values();
	void build_reference(const Model *pmodel);
	void parse_smd_triangles(Model *pmodel);
	void parse_smd_reference_skeleton(std::vector<Node> &nodes, std::vector<Bone> &bones);
	void open_smd(const std::filesystem::path &smd_path, const std::string &missing_message);
	int parse_smd_nodes(std::vector<Node> &nodes);
	void parse_smd_reference(std::filesystem::path &smd_ref_path, Model *pmodel);
	void cmd_eyeposition(std::string &token);
	void cmd_flags(std::string &token);
	void cmd_modelname(std::string &token);
	void cmd_body_option_studio(std::string &token);
	int cmd_body_option_blank();
	void cmd_bodygroup(std::string &token);
	void cmd_body(std::string &token);
	void parse_smd_animation_skeleton(Animation &anim);
	void parse_smd_animation(std::filesystem::path &sequence_smd_path, Animation &anim);
	int cmd_sequence_option_event(std::string &token, Sequence &seq);
	int cmd_sequence_option_fps(std::string &token, Sequence &seq);
	void cmd_origin(std::string &token);
	void cmd_sequence_option_origin(std::string &token);
	void cmd_sequence_option_rotate(std::string &token);
	void cmd_scale(std::string &token);
	void cmd_rotate(std::string &token);
	void cmd_sequence_option_scale(std::string &token);
	int cmd_sequence(std::string &token);
	int cmd_controller(std::string &token);
	void cmd_bbox(std::string &token);
	void cmd_cbox(std::string &token);
	void cmd_mirror(std::string &token);
	void cmd_gamma(std::string &token);
	int cmd_texturegroup(std::string &token);
	int cmd_hitgroup(std::string &token);
	int cmd_hitbox(std::string &token);
	int cmd_attachment(std::string &token);
	void cmd_renamebone(std::string &token);
	void cmd_texrendermode(std::string &token);
	void parse_qc_file(const std::filesystem::path &working_dir);

	// studiomdl.exe args -----------
	bool flag_invert_normals; // also set by "$body ... reverse"; stays set for later bodies, as in HLSDK
	bool flag_keep_all_bones;
	float normal_blend_cos; // cosine of the vertex normal blend angle

	const studiomdl::FileSystem &fs;
	QcTokenizer tokenizer;

	// SMD variables --------------------------
	FileBytes smd_bytes;
	std::string smd_file; // SMD being parsed, for error locations; empty while parsing the QC
	LineReader smdreader;
	std::string currentsmdline;
	int smdlinecount = 0;

	std::array<BoneFixUp, MAXSTUDIOSRCBONES> bonefixup{};

	struct VertexKey
	{
		int bone, x, y, z; // position in 1/100 units, truncated toward zero
		bool operator==(const VertexKey &) const = default;
	};
	struct VertexKeyHash
	{
		std::size_t operator()(const VertexKey &k) const noexcept
		{
			std::size_t h = std::hash<int>{}(k.bone);
			for (int v : {k.x, k.y, k.z})
				h = h * 1000003u ^ std::hash<int>{}(v);
			return h;
		}
	};
	std::unordered_map<VertexKey, int, VertexKeyHash> unique_vertices;

	std::array<std::uint8_t, 256> gamma_table{}; // palette byte -> gamma-corrected byte
	std::unordered_map<uint64_t, std::vector<int>> unique_normals;
};
} // namespace

// ---------------------------------------
static void clip_rotations(Vector3 rot)
{
	// clip everything to : -Q_PI <= x < Q_PI
	for (int j = 0; j < 3; j++)
	{
		while (rot[j] >= Q_PI)
			rot[j] -= Q_PI * 2;
		while (rot[j] < -Q_PI)
			rot[j] += Q_PI * 2;
	}
}

void QcCompiler::extract_motion()
{
	// extract linear motion, Relative movement distance between the first and last frame
	for (auto &sequence : qc.sequences)
	{
		if (sequence.numframes > 1)
		{
			// assume 0 for now.
			Vector3 motion{0, 0, 0};
			Vector3 *ppos = sequence.anims[0].pos[0].data();
			int start_frame = 0;
			int last_frame = sequence.numframes - 1;

			if (sequence.motiontype & STUDIO_LX)
				motion.x = ppos[last_frame].x - ppos[start_frame].x;
			if (sequence.motiontype & STUDIO_LY)
				motion.y = ppos[last_frame].y - ppos[start_frame].z;
			if (sequence.motiontype & STUDIO_LZ)
				motion.z = ppos[last_frame].z - ppos[start_frame].z;

			for (int j = 0; j < sequence.numframes; j++)
			{
				Vector3 adjusted_pos{};
				for (last_frame = 0; last_frame < sequence.anims[0].nodes.size(); last_frame++)
				{
					if (sequence.anims[0].nodes[last_frame].parent == -1)
					{
						ppos = sequence.anims[0].pos[last_frame].data();

						adjusted_pos =
							motion * (static_cast<float>(j) /
									  static_cast<float>(sequence.numframes - 1));
						for (int blend_index = 0; blend_index < sequence.anims.size();
							 blend_index++)
						{
							sequence.anims[blend_index].pos[last_frame][j] =
								sequence.anims[blend_index].pos[last_frame][j] - adjusted_pos;
						}
					}
				}
			}

			sequence.linearmovement = motion;
		}
		else
		{
			sequence.linearmovement = Vector3{0, 0, 0};
		}
	}

	// extract unused motion
	for (auto &sequence : qc.sequences)
	{
		int type_unused_motion = sequence.motiontype;
		for (int k = 0; k < sequence.anims[0].nodes.size(); k++)
		{
			if (sequence.anims[0].nodes[k].parent == -1)
			{
				for (int blend_index = 0; blend_index < sequence.anims.size();
					 blend_index++)
				{
					for (int j = 0; j < sequence.numframes; j++)
					{
						if (type_unused_motion & STUDIO_XR)
							sequence.anims[blend_index].rot[k][j].x = sequence.anims[blend_index].rot[k][0].x;
						if (type_unused_motion & STUDIO_YR)
							sequence.anims[blend_index].rot[k][j].y = sequence.anims[blend_index].rot[k][0].y;
						if (type_unused_motion & STUDIO_ZR)
							sequence.anims[blend_index].rot[k][j].z = sequence.anims[blend_index].rot[k][0].z;
					}
				}
			}
		}
	}
}

void QcCompiler::optimize_animations()
{
	// optimize animations
	for (auto &sequence : qc.sequences)
	{
		sequence.numframes =
			sequence.anims[0].endframe - sequence.anims[0].startframe + 1;

		for (const auto &blend : sequence.anims)
		{
			bool complete = blend.pos.size() >= sequence.anims[0].nodes.size();
			for (const auto &track : blend.pos)
				complete = complete && track.size() >= static_cast<std::size_t>(sequence.numframes);
			if (!complete)
			{
				error("sequence \"" + sequence.name + "\": animation \"" + blend.name +
					  "\" has fewer bones or frames than \"" + sequence.anims[0].name + "\"");
			}
		}

		// force looping animations to be looping
		if (sequence.flags & STUDIO_LOOPING)
		{
			for (int j = 0; j < sequence.anims[0].nodes.size(); j++)
			{
				for (int blends = 0; blends < sequence.anims.size(); blends++)
				{
					Vector3 *ppos = sequence.anims[blends].pos[j].data();
					Vector3 *prot = sequence.anims[blends].rot[j].data();

					int start_frame = 0;					// sequence[i].panim[q]->startframe;
					int end_frame = sequence.numframes - 1; // sequence[i].panim[q]->endframe;
					int type_motion = sequence.motiontype;

					if (!(type_motion & STUDIO_LX))
						ppos[end_frame].x = ppos[start_frame].x;
					if (!(type_motion & STUDIO_LY))
						ppos[end_frame].y = ppos[start_frame].y;
					if (!(type_motion & STUDIO_LZ))
						ppos[end_frame].z = ppos[start_frame].z;

					prot[end_frame].x = prot[start_frame].x;
					prot[end_frame].y = prot[start_frame].y;
					prot[end_frame].z = prot[start_frame].z;
				}
			}
		}

		for (auto &event : sequence.events)
		{
			if (event.frame < sequence.anims[0].startframe)
			{
				logger.warning("sequence {} has event ({}) before first frame ({})",
							sequence.name, event.frame, sequence.anims[0].startframe);
				event.frame = sequence.anims[0].startframe;
			}
			if (event.frame > sequence.anims[0].endframe)
			{
				logger.warning("sequence {} has event ({}) after last frame ({})",
							sequence.name, event.frame, sequence.anims[0].endframe);
				event.frame = sequence.anims[0].endframe;
			}
		}

		sequence.frameoffset = sequence.anims[0].startframe;
	}
}

int QcCompiler::find_node(const std::string &name)
{
	for (int k = 0; k < bonetable.size(); k++)
	{
		if (bonetable[k].name == name)
		{
			return k;
		}
	}
	return -1;
}

void QcCompiler::make_transitions()
{
	// Add in direct node transitions
	for (auto &sequence : qc.sequences)
	{
		if (sequence.entrynode != sequence.exitnode)
		{
			xnode[sequence.entrynode - 1][sequence.exitnode - 1] =
				sequence.exitnode;
			if (sequence.nodeflags)
			{
				xnode[sequence.exitnode - 1][sequence.entrynode - 1] =
					sequence.entrynode;
			}
		}
		if (sequence.entrynode > numxnodes)
			numxnodes = sequence.entrynode;
	}

	// Add multi-stage transitions
	while (true)
	{
		int hit = 0;
		for (int i = 1; i <= numxnodes; i++)
		{
			for (int j = 1; j <= numxnodes; j++)
			{
				// If I can't go there directly
				if (i != j && xnode[i - 1][j - 1] == 0)
				{
					for (int k = 1; k < numxnodes; k++)
					{
						// But I found someone who knows how that I can get to
						if (xnode[k - 1][j - 1] > 0 && xnode[i - 1][k - 1] > 0)
						{
							// Then go to them
							xnode[i - 1][j - 1] = -xnode[i - 1][k - 1];
							hit = 1;
							break;
						}
					}
				}
			}
		}

		// Reset previous pass so the links can be used in the next pass
		for (int i = 1; i <= numxnodes; i++)
		{
			for (int j = 1; j <= numxnodes; j++)
			{
				xnode[i - 1][j - 1] = abs(xnode[i - 1][j - 1]);
			}
		}

		if (!hit)
			break; // No more transitions are added
	}
}

void QcCompiler::simplify_model()
{
	std::vector<std::vector<Vector3>> defaultpos(MAXSTUDIOSRCBONES);
	std::vector<std::vector<Vector3>> defaultrot(MAXSTUDIOSRCBONES);
	int illegal_parent_bone = 0;

	optimize_animations();
	extract_motion();
	make_transitions();

	// find used bones TODO: find_used_bones()
	for (auto &submodel : qc.submodels)
	{
		for (int k = 0; k < MAXSTUDIOSRCBONES; k++)
		{
			submodel->boneref[k] = flag_keep_all_bones;
		}
		for (auto &vert : submodel->verts)
		{
			submodel->boneref[vert.bone_id] = 1;
		}
		for (int k = 0; k < submodel->nodes.size(); k++)
		{
			// tag parent bones as used if child has been used
			if (submodel->boneref[k])
			{
				int n = submodel->nodes[k].parent;
				while (n != -1 && !submodel->boneref[n])
				{
					submodel->boneref[n] = 1;
					n = submodel->nodes[n].parent;
				}
			}
		}
	}

	// rename model bones if needed TODO: rename_submodel_bones()
	for (auto &submodel : qc.submodels)
	{
		for (auto &node : submodel->nodes)
		{
			for (auto &rename : qc.renamebones)
			{
				if (node.name == rename.from)
				{
					node.name = rename.to;
					break;
				}
			}
		}
	}

	// union of all used bones TODO:create_bone_union()
	bonetable.clear();
	for (auto &submodel : qc.submodels)
	{
		for (int k = 0; k < MAXSTUDIOSRCBONES; k++)
		{
			submodel->boneimap[k] = -1;
		}
		for (int j = 0; j < submodel->nodes.size(); j++)
		{
			if (submodel->boneref[j])
			{
				int k = find_node(submodel->nodes[j].name);
				if (k == -1)
				{
					// create new bone
					k = bonetable.size();
					BoneTable newb{};
					newb.name = submodel->nodes[j].name;
					int n = submodel->nodes[j].parent;
					if (n != -1)
						newb.parent = find_node(submodel->nodes[n].name);
					else
						newb.parent = -1;
					// set defaults
					defaultpos[k].assign(MAXSTUDIOANIMATIONS, submodel->skeleton[j].pos);
					defaultrot[k].assign(MAXSTUDIOANIMATIONS, submodel->skeleton[j].rot);
					newb.pos = submodel->skeleton[j].pos;
					newb.rot = submodel->skeleton[j].rot;
					bonetable.push_back(newb);
				}
				else
				{
					// double check parent assignments
					int n = submodel->nodes[j].parent;
					if (n != -1)
						n = find_node(submodel->nodes[n].name);
					int m = bonetable[k].parent;

					if (n != m)
					{
						logger.warning("illegal parent bone replacement in model \"{}\": \"{}\" "
									"has \"{}\", previously was \"{}\"",
									submodel->name, submodel->nodes[j].name,
									(n != -1) ? bonetable[n].name : "ROOT",
									(m != -1) ? bonetable[m].name : "ROOT");
						illegal_parent_bone++;
					}
				}
				submodel->bonemap[j] = k;
				submodel->boneimap[k] = j;
			}
		}
	}

	if (illegal_parent_bone)
	{
		error("Illegal parent bone replacement in model");
	}

	if (bonetable.size() >= MAXSTUDIOBONES)
	{
		error("Too many bones used in model, used " +
			  std::to_string(bonetable.size()) + ", max " +
			  std::to_string(MAXSTUDIOBONES) + "\n");
	}

	// rename sequence bones if needed TODO: rename_sequence_bones()
	for (auto &sequence : qc.sequences)
	{
		for (auto &node : sequence.anims[0].nodes)
		{
			for (auto &rename : qc.renamebones)
			{
				if (node.name == rename.from)
				{
					node.name = rename.to;
					break;
				}
			}
		}
	}

	// map each sequences bone list to master list TODO: map_sequence_bones()
	for (auto &sequence : qc.sequences)
	{
		sequence.anims[0].boneimap.assign(MAXSTUDIOSRCBONES, -1);
		int j = 0;
		for (auto &node : sequence.anims[0].nodes)
		{
			int k = find_node(node.name);

			if (k != -1)
			{
				std::string parent_anim_name = "ROOT";
				std::string parent_bone_name = "ROOT";

				// whoa, check parent connections!
				if (node.parent != -1)
					parent_anim_name = (sequence.anims[0].nodes[node.parent].name);

				if (bonetable[k].parent != -1)
					parent_bone_name = bonetable[bonetable[k].parent].name;

				if (!case_insensitive_compare(parent_anim_name, parent_bone_name))
				{
					logger.warning("illegal parent bone replacement in sequence \"{}\": \"{}\" "
								"has \"{}\", reference has \"{}\"",
								sequence.name, node.name, parent_anim_name, parent_bone_name);
					illegal_parent_bone++;
				}
				sequence.anims[0].boneimap[k] = j;
			}
			j++;
		}
	}
	if (illegal_parent_bone)
	{
		error("Illegal parent bone replacement in sequence");
	}

	// link bonecontrollers TODO: link_bone_controllers()
	for (auto &bonecontroller : qc.bonecontrollers)
	{
		int j = 0;
		for (auto &bone : bonetable)
		{
			if (bonecontroller.name == bone.name)
				break;
			++j; // before or after if statement ?
		}

		if (j >= bonetable.size())
		{
			error("Unknown bonecontroller link '" + bonecontroller.name + "'\n");
		}
		bonecontroller.bone = j;
	}

	// link attachments TODO: link_attachments()
	for (auto &attachment : qc.attachments)
	{
		int j = 0;
		for (auto &bone : bonetable)
		{
			if (attachment.bonename == bone.name)
				break;
			++j; // before or after if statement ?
		}
		if (j >= bonetable.size())
		{
			error("Unknown attachment link '" + attachment.bonename + "'\n");
		}
		attachment.bone = j;
	}

	// relink model TODO: relink_model()
	for (auto &submodel : qc.submodels)
	{
		for (auto &vert : submodel->verts)
		{
			vert.bone_id = submodel->bonemap[vert.bone_id];
		}

		for (auto &normal : submodel->normals)
		{
			normal.bone_id = submodel->bonemap[normal.bone_id];
		}
	}

	// set hitgroups TODO: set_hit_groups()
	for (auto &bone_table : bonetable)
	{
		bone_table.group = -9999;
	}
	for (auto &hitgroup : qc.hitgroups)
	{
		int k = 0;
		for (auto &bone : bonetable)
		{
			if (bone.name == hitgroup.name)
			{
				bone.group = hitgroup.group;
				break;
			}
			++k;
		}
		if (k >= bonetable.size())
			error("cannot find bone " + hitgroup.name + " for hitgroup " +
				  std::to_string(hitgroup.group) + "\n");
	}
	for (auto &bone : bonetable)
	{
		if (bone.group == -9999)
		{
			if (bone.parent != -1)
				bone.group = bonetable[bone.parent].group;
			else
				bone.group = 0;
		}
	}

	// TODO: find_or_create_hitboxes()
	if (qc.hitboxes.empty())
	{
		// find intersection box volume for each bone
		for (auto &bone : bonetable)
		{
			for (int j = 0; j < 3; j++)
			{
				bone.bmin[j] = 0.0;
				bone.bmax[j] = 0.0;
			}
		}
		// try all the connect vertices
		for (auto &submodel : qc.submodels)
		{
			Vector3 p{};
			for (auto &vert : submodel->verts)
			{
				p = vert.pos;
				int k = vert.bone_id;

				if (p[0] < bonetable[k].bmin[0])
					bonetable[k].bmin[0] = p[0];
				if (p[1] < bonetable[k].bmin[1])
					bonetable[k].bmin[1] = p[1];
				if (p[2] < bonetable[k].bmin[2])
					bonetable[k].bmin[2] = p[2];
				if (p[0] > bonetable[k].bmax[0])
					bonetable[k].bmax[0] = p[0];
				if (p[1] > bonetable[k].bmax[1])
					bonetable[k].bmax[1] = p[1];
				if (p[2] > bonetable[k].bmax[2])
					bonetable[k].bmax[2] = p[2];
			}
		}
		// add in all your children as well
		for (auto &bone : bonetable)
		{
			int j = bone.parent;
			if (j != -1)
			{
				if (bone.pos[0] < bonetable[j].bmin[0])
					bonetable[j].bmin[0] = bone.pos[0];
				if (bone.pos[1] < bonetable[j].bmin[1])
					bonetable[j].bmin[1] = bone.pos[1];
				if (bone.pos[2] < bonetable[j].bmin[2])
					bonetable[j].bmin[2] = bone.pos[2];
				if (bone.pos[0] > bonetable[j].bmax[0])
					bonetable[j].bmax[0] = bone.pos[0];
				if (bone.pos[1] > bonetable[j].bmax[1])
					bonetable[j].bmax[1] = bone.pos[1];
				if (bone.pos[2] > bonetable[j].bmax[2])
					bonetable[j].bmax[2] = bone.pos[2];
			}
		}

		int k = 0;
		for (auto &bone : bonetable)
		{
			if (bone.bmin[0] < bone.bmax[0] - 1 && bone.bmin[1] < bone.bmax[1] - 1 &&
				bone.bmin[2] < bone.bmax[2] - 1)
			{
				HitBox genhbox{};
				genhbox.bone = k;
				genhbox.group = bone.group;
				genhbox.bmin = bone.bmin;
				genhbox.bmax = bone.bmax;
				qc.hitboxes.push_back(genhbox);
			}
			k++;
		}
	}
	else
	{
		for (auto &hitbox : qc.hitboxes)
		{
			int k = 0;
			for (auto &bone : bonetable)
			{
				if (bone.name == hitbox.name)
				{
					hitbox.bone = k;
					break;
				}
				k++;
			}
			if (k >= bonetable.size())
				error("cannot find bone " + hitbox.name + " for bbox\n");
		}
	}

	// relink animations TODO: relink_animations()
	for (auto &sequence : qc.sequences)
	{
		// Kept across blends: a blend with fewer nodes sees the previous blend's tracks, as before.
		std::vector<std::vector<Vector3>> origpos(MAXSTUDIOSRCBONES);
		std::vector<std::vector<Vector3>> origrot(MAXSTUDIOSRCBONES);
		// Only the first numframes frames are ever read from the rest-pose tracks.
		const auto frames = static_cast<std::ptrdiff_t>(std::min(sequence.numframes, MAXSTUDIOANIMATIONS));

		for (int q = 0; q < sequence.anims.size(); q++)
		{
			Animation &anim = sequence.anims[q];

			// take the original animations, indexed by node
			for (int j = 0; j < anim.nodes.size(); j++)
			{
				origpos[j] = std::move(anim.pos[j]);
				origrot[j] = std::move(anim.rot[j]);
			}

			// re-index them by bone table index
			anim.pos.assign(bonetable.size(), {});
			anim.rot.assign(bonetable.size(), {});
			for (int j = 0; j < bonetable.size(); j++)
			{
				int k = sequence.anims[0].boneimap[j];
				if (k >= 0)
				{
					// link to original animations
					anim.pos[j] = origpos[k];
					anim.rot[j] = origrot[k];
				}
				else
				{
					// link to dummy animations
					anim.pos[j].assign(defaultpos[j].begin(), defaultpos[j].begin() + frames);
					anim.rot[j].assign(defaultrot[j].begin(), defaultrot[j].begin() + frames);
				}
			}
		}
	}

	// find scales for all bones TODO: find_bone_scales()
	for (int j = 0; j < bonetable.size(); j++)
	{
		for (int k = 0; k < DEGREESOFFREEDOM; k++)
		{
			float minv, maxv, scale;

			if (k < 3)
			{
				minv = -128.0;
				maxv = 128.0;
			}
			else
			{
				minv = -Q_PI / 8.0;
				maxv = Q_PI / 8.0;
			}

			for (auto &sequence : qc.sequences)
			{
				for (auto &anim : sequence.anims)
				{
					for (int n = 0; n < sequence.numframes; n++)
					{
						float v;
						switch (k)
						{
						case 0:
						case 1:
						case 2:
							v = (anim.pos[j][n][k] - bonetable[j].pos[k]);
							break;
						case 3:
						case 4:
						case 5:
							v = (anim.rot[j][n][k - 3] - bonetable[j].rot[k - 3]);
							if (v >= Q_PI)
								v -= Q_PI * 2;
							if (v < -Q_PI)
								v += Q_PI * 2;
							break;
						}
						if (v < minv)
							minv = v;
						if (v > maxv)
							maxv = v;
					}
				}
			}
			if (minv < maxv)
			{
				if (-minv > maxv)
				{
					scale = minv / -32768.0f;
				}
				else
				{
					scale = maxv / 32767.0f;
				}
			}
			else
			{
				scale = 1.0 / 32.0;
			}
			switch (k)
			{
			case 0:
			case 1:
			case 2:
				bonetable[j].posscale[k] = scale;
				break;
			case 3:
			case 4:
			case 5:
				bonetable[j].rotscale[k - 3] = scale;
				break;
			}
		}
	}

	// find bounding box for each sequence TODO: find_sequence_bounding_boxes()
	for (auto &sequence : qc.sequences)
	{
		Vector3 bmin{9999.0, 9999.0, 9999.0};
		Vector3 bmax{-9999.0, -9999.0, -9999.0};

		// find intersection box volume for each bone
		for (auto &anim : sequence.anims)
		{
			for (int n = 0; n < sequence.numframes; n++)
			{
				std::array<Matrix3x4, MAXSTUDIOBONES> bonetransform{}; // bone transformation matrix
				Matrix3x4 bonematrix{};								   // local transformation matrix
				Vector3 pos{};
				int j = 0;
				for (auto &bone : bonetable)
				{
					Vector3 angles{
						anim.rot[j][n][0],
						anim.rot[j][n][1],
						anim.rot[j][n][2]};

					bonematrix = angle_matrix(angles);

					bonematrix[0][3] = anim.pos[j][n][0];
					bonematrix[1][3] = anim.pos[j][n][1];
					bonematrix[2][3] = anim.pos[j][n][2];

					if (bone.parent == -1)
					{
						matrix_copy(bonematrix, bonetransform[j]);
					}
					else
					{
						bonetransform[j] =
							concat_transforms(bonetransform[bone.parent], bonematrix);
					}
					j++;
				}

				for (auto &submodel : qc.submodels)
				{
					for (auto &vert : submodel->verts)
					{
						pos = vector_transform(vert.pos, bonetransform[vert.bone_id]);

						if (pos[0] < bmin[0])
							bmin[0] = pos[0];
						if (pos[1] < bmin[1])
							bmin[1] = pos[1];
						if (pos[2] < bmin[2])
							bmin[2] = pos[2];
						if (pos[0] > bmax[0])
							bmax[0] = pos[0];
						if (pos[1] > bmax[1])
							bmax[1] = pos[1];
						if (pos[2] > bmax[2])
							bmax[2] = pos[2];
					}
				}
			}
		}

		sequence.bmin = bmin;
		sequence.bmax = bmax;
	}

	// reduce animations TODO: reduce_animations()
	{
		int changes = 0;

		for (auto &sequence : qc.sequences)
		{
			for (auto &anim : sequence.anims)
			{
				anim.anims.assign(bonetable.size(), {});
				for (int j = 0; j < bonetable.size(); j++)
				{
					for (int k = 0; k < DEGREESOFFREEDOM; k++)
					{
						float v;
						std::array<short, MAXSTUDIOANIMATIONS> value{};
						std::array<StudioAnimationValue, MAXSTUDIOANIMATIONS> data{};
						int n;
						for (n = 0; n < sequence.numframes; n++)
						{
							switch (k)
							{
							case 0:
							case 1:
							case 2:
								value[n] = static_cast<short>(
									(anim.pos[j][n][k] - bonetable[j].pos[k]) /
									bonetable[j].posscale[k]);
								break;
							case 3:
							case 4:
							case 5:
								v = (anim.rot[j][n][k - 3] - bonetable[j].rot[k - 3]);
								if (v >= Q_PI)
									v -= Q_PI * 2;
								if (v < -Q_PI)
									v += Q_PI * 2;

								value[n] =
									static_cast<short>(v / bonetable[j].rotscale[k - 3]);
								break;
							}
						}
						if (n == 0)
							error("no animation frames: " + sequence.name + "\n");

						std::memset(data.data(), 0, sizeof(data));
						StudioAnimationValue *pcount = data.data();
						StudioAnimationValue *pvalue = pcount + 1;

						pcount->num.valid = 1;
						pcount->num.total = 1;
						pvalue->value = value[0];
						pvalue++;

						for (int m = 1, p = 0; m < n; m++)
						{
							if (abs(value[p] - value[m]) > 1600)
							{
								changes++;
								p = m;
							}
						}

						// this compression algorithm needs work

						for (int m = 1; m < n; m++)
						{
							if (pcount->num.total == 255)
							{
								// too many, force a new entry
								pcount = pvalue;
								pvalue = pcount + 1;
								pcount->num.valid++;
								pvalue->value = value[m];
								pvalue++;
							}
							// insert value if they're not equal,
							// or if we're not on a run and the run is less than 3 units
							else if ((value[m] != value[m - 1]) ||
									 ((pcount->num.total == pcount->num.valid) &&
									  ((m < n - 1) && value[m] != value[m + 1])))
							{
								if (pcount->num.total != pcount->num.valid)
								{
									pcount = pvalue;
									pvalue = pcount + 1;
								}
								pcount->num.valid++;
								pvalue->value = value[m];
								pvalue++;
							}
							pcount->num.total++;
						}

						// a constant zero track is stored as no values at all
						if (!(pvalue - data.data() == 2 && value[0] == 0))
						{
							anim.anims[j][k].assign(data.data(), pvalue);
						}
					}
				}
			}
		}
	}
}

static int lookup_control(const std::string &token)
{
	if (case_insensitive_compare(token, "X"))
		return STUDIO_X;
	if (case_insensitive_compare(token, "Y"))
		return STUDIO_Y;
	if (case_insensitive_compare(token, "Z"))
		return STUDIO_Z;
	if (case_insensitive_compare(token, "XR"))
		return STUDIO_XR;
	if (case_insensitive_compare(token, "YR"))
		return STUDIO_YR;
	if (case_insensitive_compare(token, "ZR"))
		return STUDIO_ZR;
	if (case_insensitive_compare(token, "LX"))
		return STUDIO_LX;
	if (case_insensitive_compare(token, "LY"))
		return STUDIO_LY;
	if (case_insensitive_compare(token, "LZ"))
		return STUDIO_LZ;
	if (token == "AX" || token == "AY" || token == "AZ" ||
		token == "AXR" || token == "AYR" || token == "AZR")
		return 0;

	return -1;
}

int QcCompiler::find_texture_index(const std::string &texturename) // Common QC and SMD parser
{
	int i = 0;
	for (auto &texture : textures)
	{
		if (texture.name == texturename)
		{
			return i;
		}
		i++;
	}
	Texture newtexture{};
	newtexture.name = texturename;

	std::string lower_texname{texturename};
	std::transform(lower_texname.begin(), lower_texname.end(),
				   lower_texname.begin(), ::tolower);
	if (lower_texname.find("chrome") != std::string::npos)
		newtexture.flags = STUDIO_NF_FLATSHADE | STUDIO_NF_CHROME;
	else if (lower_texname.find("bright") != std::string::npos)
		newtexture.flags = STUDIO_NF_FLATSHADE | STUDIO_NF_FULLBRIGHT;
	else
		newtexture.flags = 0;

	textures.push_back(newtexture);
	return i;
}

Mesh *QcCompiler::find_mesh_by_texture(Model *pmodel, const std::string &texturename) // SMD Parser
{
	int i;
	int j = find_texture_index(texturename);

	for (i = 0; i < pmodel->nummesh; i++)
	{
		if (pmodel->pmeshes[i]->skinref == j)
		{
			return pmodel->pmeshes[i].get();
		}
	}

	if (i >= MAXSTUDIOMESHES)
	{
		error("too many textures in model: \"" + std::string(pmodel->name) +
			  "\"\n");
	}

	pmodel->nummesh = i + 1;
	pmodel->pmeshes[i] = std::make_unique<Mesh>();
	pmodel->pmeshes[i]->skinref = j;

	return pmodel->pmeshes[i].get();
}

static TriangleVert *find_mesh_triangle_by_index(Mesh *pmesh, const int index)
{
	if (index >= static_cast<int>(pmesh->triangles.size()))
	{
		pmesh->triangles.resize(index + 256);
	}

	return pmesh->triangles[index].data();
}

int QcCompiler::find_vertex_normal_index(Model *pmodel, const Normal *pnormal)
{
	const int16_t id = static_cast<int16_t>(pnormal->bone_id);
	const int16_t sr = static_cast<int16_t>(pnormal->skinref);

	// ParticleMDL: keyed on the bone and the texture only, so every earlier normal of both is
	// compared, in the order they came, as the original studiomdl does. With the direction in the
	// key too, two normals under the blend angle apart that rounded differently were both kept.
	const uint64_t key = (static_cast<uint64_t>(static_cast<uint16_t>(id)) << 48) |
						 (static_cast<uint64_t>(static_cast<uint16_t>(sr)) << 32);

	auto it = unique_normals.find(key);
	if (it != unique_normals.end())
	{
		for (int index : it->second)
		{
			const auto &normal = pmodel->normals[index];

			if (normal.pos.dot(pnormal->pos) > normal_blend_cos)
			{
				return index;
			}
		}
	}

	const int index = static_cast<int>(pmodel->normals.size());
	if (index >= MAXSTUDIOVERTS)
	{
		error("too many normals in model: \"" + std::string(pmodel->name) + "\"\n");
	}

	pmodel->normals.push_back(*pnormal);
	unique_normals[key].push_back(index);

	return index;
}

int QcCompiler::find_vertex_index(Model *pmodel, Vertex *pv)
{
	for (int axis = 0; axis < 3; axis++)
	{
		if (!(std::abs(pv->pos[axis]) < 1.0e7f)) // also rejects NaN
		{
			error("vertex coordinate out of range in model \"" + pmodel->name + "\"");
		}
	}
	const VertexKey key{pv->bone_id, static_cast<int>(pv->pos[0] * 100.0f), static_cast<int>(pv->pos[1] * 100.0f),
						static_cast<int>(pv->pos[2] * 100.0f)};

	auto it = unique_vertices.find(key);
	if (it != unique_vertices.end())
	{
		return it->second;
	}

	const int index = static_cast<int>(pmodel->verts.size());
	if (index >= MAXSTUDIOVERTS)
	{
		error("too many vertices in model: \"" + pmodel->name + "\"\n");
	}

	// assume 2 digits of accuracy
	pv->pos[0] = static_cast<int>(pv->pos[0] * 100.0f) / 100.0f;
	pv->pos[1] = static_cast<int>(pv->pos[1] * 100.0f) / 100.0f;
	pv->pos[2] = static_cast<int>(pv->pos[2] * 100.0f) / 100.0f;

	pmodel->verts.push_back(*pv);
	unique_vertices[key] = index;

	return index;
}

// Called for the base frame
static void texture_coord_ranges(Mesh *pmesh, Texture *ptexture)
{
	if (ptexture->flags & STUDIO_NF_CHROME)
	{
		ptexture->skintop = 0;
		ptexture->skinleft = 0;
		ptexture->skinwidth = (ptexture->srcwidth + 3) & ~3;
		ptexture->skinheight = ptexture->srcheight;

		for (int i = 0; i < pmesh->numtris; i++)
		{
			for (int j = 0; j < 3; j++)
			{
				pmesh->triangles[i][j].s = 0;
				pmesh->triangles[i][j].t = +1; // ??
			}
			ptexture->max_s = 63;
			ptexture->min_s = 0;
			ptexture->max_t = 63;
			ptexture->min_t = 0;
		}
		return;
	}

	// convert to pixel coordinates
	for (int i = 0; i < pmesh->numtris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			// round to fix UV shift
			pmesh->triangles[i][j].s = static_cast<int>(std::round(
				pmesh->triangles[i][j].u * static_cast<float>(ptexture->srcwidth)));
			pmesh->triangles[i][j].t = static_cast<int>(std::round(
				pmesh->triangles[i][j].v * static_cast<float>(ptexture->srcheight)));
		}
	}

	// find the range
	ptexture->max_s = static_cast<float>(ptexture->srcwidth);
	ptexture->min_s = 0;
	ptexture->max_t = static_cast<float>(ptexture->srcheight);
	ptexture->min_t = 0;
}

static void reset_texture_coord_ranges(Mesh *pmesh, const Texture *ptexture)
{
	// adjust top, left edge
	for (int i = 0; i < pmesh->numtris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			pmesh->triangles[i][j].t = -pmesh->triangles[i][j].t +
									   ptexture->srcheight -
									   static_cast<int>(ptexture->min_t);
		}
	}
}

static void grab_bmp(FileBytes file, const std::string &filename, Texture *ptexture)
{
	if (int result = load_bmp(file.bytes(), ptexture->layout, ptexture->palette,
							  &ptexture->srcwidth, &ptexture->srcheight))
	{
		const char *reason = "unreadable BMP file";
		switch (result)
		{
		case -1:
			reason = "unable to open BMP file";
			break;
		case -3000:
			reason = "invalid BMP file header";
			break;
		case -4:
			reason = "BMP file not 8 bit";
			break;
		case -5:
			reason = "invalid BMP compression type";
			break;
		}
		error("error " + std::to_string(result) + " reading BMP image \"" +
			  filename + "\": " + reason + "\n");
	}
	ptexture->image = std::move(file);
}

void QcCompiler::resize_texture(Texture *ptexture)
{
	// Keep the original texture without resizing to avoid uv shift
	ptexture->skintop = static_cast<int>(ptexture->min_t);
	ptexture->skinleft = static_cast<int>(ptexture->min_s);
	ptexture->skinwidth = ptexture->srcwidth;
	ptexture->skinheight = ptexture->srcheight;

	ptexture->size = ptexture->skinwidth * ptexture->skinheight + 256 * 3;

	logger.info("\t {} [{} {}] ({:.0f}%)  {:6} bytes", ptexture->name,
			 ptexture->skinwidth, ptexture->skinheight,
			 (static_cast<float>(ptexture->skinwidth * ptexture->skinheight) /
			  static_cast<float>(ptexture->srcwidth * ptexture->srcheight)) *
				 100.0f,
			 ptexture->size);

	if (ptexture->size > 1024 * 1024) // TODO: change this, 640 * 480 in hlsdk
	{
		error(std::format("Texture too large: \"{}\" uses {} bytes (s {:.0f}..{:.0f}, t {:.0f}..{:.0f})\n",
						  ptexture->name, ptexture->size, ptexture->min_s, ptexture->max_s,
						  ptexture->min_t, ptexture->max_t));
	}
	// The pixels are copied from the BMP straight into the .mdl by the writer.
	// TODO: process the texture and flag it if fullbright or transparent are
	// used.
	// TODO: only save as many palette entries as are actually used.
	if (qc.gamma != 1.8f)
	// gamma correct the monster textures to a gamma of 1.8
	{
		for (std::uint8_t &value : ptexture->palette)
		{
			value = gamma_table[value];
		}
	}
}

void QcCompiler::grab_skin(Texture *ptexture)
{
	std::filesystem::path texture_path{(qc.cdtexture / ptexture->name).lexically_normal()};
	auto texture_file = FileBytes::load(fs, texture_path.generic_string());
	if (!texture_file)
	{
		error("Cannot find \"" + ptexture->name + "\" texture in \"" +
			  qc.cdtexture.string() + "\" or path does not exist\n");
	}
	if (case_insensitive_compare(texture_path.extension().string(), ".bmp"))
	{
		grab_bmp(std::move(*texture_file), texture_path.string(), ptexture);
	}
	else
	{
		error("Not supported texture format: \"" + texture_path.string() +
			  "\"\n");
	}
}

void QcCompiler::set_skin_values()
{

	logger.info("Grabbing texture:");
	if (qc.gamma != 1.8f)
	{
		const float g = qc.gamma / 1.8f;
		for (int value = 0; value < 256; value++)
		{
			// in 0..255 for any gamma > 0; clamped so nonsense gammas (<= 0, NaN) stay defined
			const double corrected = std::round(pow(value / 255.0, g) * 255);
			gamma_table[value] = static_cast<uint8_t>(corrected >= 0 ? std::min(corrected, 255.0) : 0.0);
		}
	}
	for (auto &texture : textures)
	{
		grab_skin(&texture);

		texture.max_s = -9999999;
		texture.min_s = 9999999;
		texture.max_t = -9999999;
		texture.min_t = 9999999;
	}

	for (auto &submodel : qc.submodels)
	{
		for (int j = 0; j < submodel->nummesh; j++)
		{
			texture_coord_ranges(submodel->pmeshes[j].get(),
								 &textures[submodel->pmeshes[j]->skinref]);
		}
	}

	for (auto &texture : textures)
	{
		if (texture.max_s < texture.min_s)
		{
			// must be a replacement texture
			if (texture.flags & STUDIO_NF_CHROME)
			{
				texture.max_s = 63;
				texture.min_s = 0;
				texture.max_t = 63;
				texture.min_t = 0;
			}
			else
			{
				texture.max_s = textures[texture.parent].max_s;
				texture.min_s = textures[texture.parent].min_s;
				texture.max_t = textures[texture.parent].max_t;
				texture.min_t = textures[texture.parent].min_t;
			}
		}

		resize_texture(&texture);
	}

	for (auto &submodel : qc.submodels)
	{
		for (int j = 0; j < submodel->nummesh; j++)
		{
			reset_texture_coord_ranges(submodel->pmeshes[j].get(),
									   &textures[submodel->pmeshes[j]->skinref]);
		}
	}

	// build texture groups
	int i;
	for (i = 0; i < MAXSTUDIOSKINS; i++)
	{
		for (int j = 0; j < MAXSTUDIOSKINS; j++)
		{
			skinref[i][j] = j;
		}
	}
	for (i = 0; i < qc.texturegroup_rows; i++)
	{
		for (int j = 0; j < qc.texturegroup_cols; j++)
		{
			skinref[i][qc.texturegroups[0][j]] = qc.texturegroups[i][j];
		}
	}
	if (i != 0)
	{
		skinfamiliescount = i;
	}
	else
	{
		skinfamiliescount = 1;
		skinrefcount = textures.size();
	}
}

void QcCompiler::build_reference(const Model *pmodel)
{
	Vector3 bone_angles{};

	for (int i = 0; i < pmodel->nodes.size(); i++)
	{
		// convert to degrees
		bone_angles[0] = pmodel->skeleton[i].rot[0];
		bone_angles[1] = pmodel->skeleton[i].rot[1];
		bone_angles[2] = pmodel->skeleton[i].rot[2];

		int parent = pmodel->nodes[i].parent;
		if (parent == -1)
		{
			// scale the done pos.
			// calc rotational matrices
			bonefixup[i].matrix = angle_matrix(bone_angles);
			bonefixup[i].inv_matrix = angle_i_matrix(bone_angles);
			bonefixup[i].worldorg = pmodel->skeleton[i].pos;
		}
		else
		{
			// calc compound rotational matrices
			// FIXME : Hey, it's orthogical so inv(A) == transpose(A)
			Matrix3x4 rotation_matrix = angle_matrix(bone_angles);
			bonefixup[i].matrix =
				concat_transforms(bonefixup[parent].matrix, rotation_matrix);
			rotation_matrix = angle_i_matrix(bone_angles);
			bonefixup[i].inv_matrix =
				concat_transforms(rotation_matrix, bonefixup[parent].inv_matrix);

			// calc true world coord.
			Vector3 true_pos =
				vector_transform(pmodel->skeleton[i].pos, bonefixup[parent].matrix);
			bonefixup[i].worldorg = true_pos + bonefixup[parent].worldorg;
		}
	}
}

void QcCompiler::parse_smd_triangles(Model *pmodel)
{
	Vector3 vmin{99999, 99999, 99999};

	build_reference(pmodel);

	unique_vertices.clear();
	unique_normals.clear();

	// load the base triangles
	while (true)
	{
		if (smdreader.next(currentsmdline))
		{
			TriangleVert *ptriangle_vert;
			int parent_bone;

			std::array<Vector3, 3> triangle_vertices{};
			std::array<Vector3, 3> triangle_normals{};

			smdlinecount++;
			if (case_insensitive_compare("end", currentsmdline))
				return;

			std::string material{currentsmdline};
			// strip off trailing smag
			material.erase(std::find_if(material.rbegin(), material.rend(), [](unsigned char ch)
										{ return std::isgraph(ch); })
							   .base(),
						   material.end());

			Mesh *pmesh = find_mesh_by_texture(pmodel, material);

			for (int j = 0; j < 3; j++)
			{
				if (flag_invert_normals)
					ptriangle_vert =
						find_mesh_triangle_by_index(pmesh, pmesh->numtris) + j;
				else // quake wants them in the reverse order
					ptriangle_vert =
						find_mesh_triangle_by_index(pmesh, pmesh->numtris) + 2 - j;

				if (smdreader.next(currentsmdline))
				{
					Vertex triangle_vertex{};
					Normal triangle_normal{};
					LineScanner line{currentsmdline};
					smdlinecount++;
					if (line.read_all(parent_bone, triangle_vertex.pos.x,
									  triangle_vertex.pos.y, triangle_vertex.pos.z,
									  triangle_normal.pos.x, triangle_normal.pos.y,
									  triangle_normal.pos.z, ptriangle_vert->u, ptriangle_vert->v))
					{
						if (parent_bone < 0 || parent_bone >= pmodel->nodes.size())
						{
							error("Bogus bone index at line " +
								  std::to_string(smdlinecount));
						}

						triangle_vertices[j] = triangle_vertex.pos;
						triangle_normals[j] = triangle_normal.pos;

						triangle_vertex.bone_id = parent_bone;
						triangle_normal.bone_id = parent_bone;
						triangle_normal.skinref = pmesh->skinref;

						if (triangle_vertex.pos[2] < vmin[2])
							vmin[2] = triangle_vertex.pos[2];

						triangle_vertex.pos -=
							qc.sequence_origin;							   // adjust vertex to origin
						triangle_vertex.pos *= qc.scale_body_and_sequence; // scale vertex

						// move vertex position to object space.
						Vector3 tmp = triangle_vertex.pos -
									  bonefixup[triangle_vertex.bone_id].worldorg;
						triangle_vertex.pos = vector_transform(
							tmp, bonefixup[triangle_vertex.bone_id].inv_matrix);

						// move normal to object space.
						tmp = triangle_normal.pos;
						triangle_normal.pos = vector_transform(
							tmp, bonefixup[triangle_vertex.bone_id].inv_matrix);
						triangle_normal.pos.normalize();

						ptriangle_vert->normindex =
							find_vertex_normal_index(pmodel, &triangle_normal);
						ptriangle_vert->vertindex =
							find_vertex_index(pmodel, &triangle_vertex);
					}
					else
					{
						error("Triangles line " + std::to_string(smdlinecount) + ": " +
							  currentsmdline);
					}
				}
			}

			pmesh->numtris++;
		}
		else
		{
			break;
		}
	}

	if (vmin[2] != 0.0)
	{
		logger.info("Lowest vector at {:f}", vmin[2]);
	}
}

void QcCompiler::parse_smd_reference_skeleton(std::vector<Node> &nodes,
										 std::vector<Bone> &bones)
{
	std::string cmd;
	int node;
	float posX, posY, posZ, rotX, rotY, rotZ;

	while (smdreader.next(currentsmdline))
	{
		smdlinecount++;
		LineScanner line{currentsmdline};

		if (line.read_all(node, posX, posY, posZ, rotX, rotY, rotZ))
		{
			if (node < 0 || node >= static_cast<int>(nodes.size()))
			{
				error("Bogus bone index at line " + std::to_string(smdlinecount));
			}
			bones.emplace_back();
			bones.back().pos = Vector3{posX, posY, posZ};
			bones.back().pos *= qc.scale_body_and_sequence;

			if (nodes[node].mirrored)
				bones.back().pos *= -1.0;
			bones.back().rot = Vector3{rotX, rotY, rotZ};
			clip_rotations(bones.back().rot);
		}
		else
		{
			LineScanner retry{currentsmdline};
			if (retry.read_word(cmd) && case_insensitive_compare(cmd, "end"))
				break;
		}
	}
	if (bones.size() < nodes.size())
	{
		// Bones without a rest pose get the identity; they are normally unused and dropped.
		logger.warning("reference skeleton has {} bones but {} nodes; missing bones get a zero rest pose",
					   bones.size(), nodes.size());
		bones.resize(nodes.size());
	}
}

void QcCompiler::open_smd(const std::filesystem::path &smd_path, const std::string &missing_message)
{
	auto file = FileBytes::load(fs, smd_path.generic_string());
	if (!file)
	{
		error(missing_message);
	}
	smd_bytes = std::move(*file);
	smd_file = smd_path.string();
	smdreader = LineReader{smd_bytes.text()};
}

int QcCompiler::parse_smd_nodes(std::vector<Node> &nodes)
{
	int index;
	std::string bone_name;
	int parent;

	while (smdreader.next(currentsmdline))
	{
		smdlinecount++;
		LineScanner line{currentsmdline};
		if (line.read(index) && line.read_quoted(bone_name) && line.read(parent))
		{
			if (nodes.size() >= MAXSTUDIOSRCBONES)
			{
				error("too many nodes (max " + std::to_string(MAXSTUDIOSRCBONES) + ")");
			}
			if (parent < -1 || parent >= static_cast<int>(nodes.size()))
			{
				error("node \"" + bone_name + "\" has parent " + std::to_string(parent) +
					  ", which is not an earlier node");
			}
			nodes.emplace_back();
			nodes.back().name = bone_name;
			nodes.back().parent = parent;

			// Check for mirrored bones
			for (int i = 0; i < qc.mirroredbones.size(); i++)
			{
				if (case_insensitive_compare(bone_name, qc.mirroredbones[i]))
				{
					nodes.back().mirrored = 1;
				}
			}

			if ((!nodes.back().mirrored) && parent != -1)
			{
				nodes.back().mirrored = nodes[parent].mirrored;
			}
		}
		else
		{
			return 1;
		}
	}
	error("Unexpected EOF at line " + std::to_string(smdlinecount) + "\n");
	return 0;
}

void QcCompiler::parse_smd_reference(std::filesystem::path &smd_ref_path, Model *pmodel)
{
	std::filesystem::path smd_path;
	std::string cmd;
	int smd_version;
	smdlinecount = 0;

	if (!case_insensitive_compare(smd_ref_path.extension().string(), ".smd"))
	{
		smd_ref_path += ".smd";
	}

	if (smd_ref_path.is_relative())
	{
		smd_path = (qc.cd / smd_ref_path).lexically_normal();
	}
	else
	{
		smd_path = smd_ref_path;
	}

	open_smd(smd_path, "Cannot find \"" + pmodel->name + "\" in " + smd_ref_path.string() + "\"\n");

	logger.info("Grabbing reference: {}", smd_path.string());

	while (smdreader.next(currentsmdline))
	{
		smdlinecount++;
		LineScanner line{currentsmdline};

		if (!line.read_word(cmd))
			continue;
		if (case_insensitive_compare(cmd, "version"))
		{
			if (!line.read(smd_version))
			{
				error("Missing SMD version number.\n");
				return;
			}
			if (smd_version != 1)
			{
				error("Unsupported SMD version: " + std::to_string(smd_version) + "\n");
				return;
			}
		}
		else if (case_insensitive_compare(cmd, "nodes"))
		{
			parse_smd_nodes(pmodel->nodes);
		}
		else if (case_insensitive_compare(cmd, "skeleton"))
		{
			parse_smd_reference_skeleton(pmodel->nodes, pmodel->skeleton);
		}
		else if (case_insensitive_compare(cmd, "triangles"))
		{
			if (pmodel->skeleton.size() < pmodel->nodes.size())
			{
				error("triangles before the skeleton");
			}
			parse_smd_triangles(pmodel);
		}
	}
	if (pmodel->skeleton.size() < pmodel->nodes.size())
	{
		error("missing skeleton section");
	}
	smd_file.clear();
}

void QcCompiler::cmd_eyeposition(std::string &token)
{
	// rotate points into frame of reference so model points down the positive x
	// axis
	tokenizer.get_token(false, token);
	qc.eyeposition[1] = std::stof(token);

	tokenizer.get_token(false, token);
	qc.eyeposition[0] = -std::stof(token);

	tokenizer.get_token(false, token);
	qc.eyeposition[2] = std::stof(token);
}

void QcCompiler::cmd_flags(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.flags = std::stoi(token);
}

void QcCompiler::cmd_modelname(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.modelname = token;
}

void QcCompiler::cmd_body_option_studio(std::string &token)
{
	if (!tokenizer.get_token(false, token))
		return;

	std::replace(token.begin(), token.end(), '\\', '/');
	std::filesystem::path smd_ref_path{token};
	auto new_submodel = std::make_unique<Model>();

	new_submodel->name = smd_ref_path.stem().string();

	qc.scale_body_and_sequence = qc.scale;

	while (tokenizer.token_available())
	{
		tokenizer.get_token(false, token);
		if (case_insensitive_compare("reverse", token))
		{
			flag_invert_normals = true;
		}
		else if (case_insensitive_compare("scale", token))
		{
			tokenizer.get_token(false, token);
			qc.scale_body_and_sequence = std::stof(token);
		}
	}

	parse_smd_reference(smd_ref_path, new_submodel.get());

	qc.submodels.push_back(std::move(new_submodel));
	qc.bodyparts.back().num_submodels++;

	qc.scale_body_and_sequence = qc.scale;
}

int QcCompiler::cmd_body_option_blank()
{
	auto new_submodel = std::make_unique<Model>();

	new_submodel->name = "blank";

	qc.submodels.push_back(std::move(new_submodel));
	qc.bodyparts.back().num_submodels++;
	return 0;
}

void QcCompiler::cmd_bodygroup(std::string &token)
{
	if (!tokenizer.get_token(false, token))
		return;

	BodyPart newbp{};

	if (qc.bodyparts.empty())
	{
		newbp.base = 1;
	}
	else
	{
		BodyPart &lastbp = qc.bodyparts.back();
		newbp.base = lastbp.base * lastbp.num_submodels;
	}

	newbp.name = token;
	qc.bodyparts.push_back(newbp);

	while (true)
	{
		tokenizer.get_token(true, token);
		if (tokenizer.end_of_file())
			return;
		if (token[0] == '}')
		{
			break;
		}
		else if (case_insensitive_compare("studio", token))
		{
			cmd_body_option_studio(token);
		}
		else if (case_insensitive_compare("blank", token))
		{
			cmd_body_option_blank();
		}
	}
}

void QcCompiler::cmd_body(std::string &token)
{
	if (!tokenizer.get_token(false, token))
		return;

	BodyPart newbp{};
	newbp.name = token;

	if (qc.bodyparts.empty())
	{
		newbp.base = 1;
	}
	else
	{
		BodyPart &lastbp = qc.bodyparts.back();
		newbp.base = lastbp.base * lastbp.num_submodels;
	}

	qc.bodyparts.push_back(newbp);
	cmd_body_option_studio(token);
}

void QcCompiler::parse_smd_animation_skeleton(Animation &anim)
{
	Vector3 pos;
	Vector3 rot;
	int index;
	int t = -99999999;
	int start = 99999;
	int end = 0;

	anim.pos.assign(anim.nodes.size(), std::vector<Vector3>(MAXSTUDIOANIMATIONS));
	anim.rot.assign(anim.nodes.size(), std::vector<Vector3>(MAXSTUDIOANIMATIONS));

	const float cosz = std::cos(qc.rotate);
	const float sinz = std::sin(qc.rotate);

	while (smdreader.next(currentsmdline))
	{
		smdlinecount++;
		LineScanner line{currentsmdline};
		if (line.read_all(index, pos.x, pos.y, pos.z, rot.x, rot.y, rot.z))
		{
			if (t >= anim.startframe && t <= anim.endframe)
			{
				if (t < 0 || t >= MAXSTUDIOANIMATIONS)
				{
					error("frame " + std::to_string(t) + " out of range in \"" + anim.name + "\" (max " +
						  std::to_string(MAXSTUDIOANIMATIONS - 1) + ")");
				}
				if (index < 0 || index >= static_cast<int>(anim.nodes.size()))
				{
					error("Bogus bone index at line " + std::to_string(smdlinecount));
				}
				if (anim.nodes[index].parent == -1)
				{
					pos -= qc.sequence_origin; // adjust vertex to origin
					anim.pos[index][t].x = cosz * pos.x - sinz * pos.y;
					anim.pos[index][t].y = sinz * pos.x + cosz * pos.y;
					anim.pos[index][t].z = pos.z;
					// rotate model
					rot.z += qc.rotate;
				}
				else
				{
					anim.pos[index][t] = pos;
				}
				if (t > end)
					end = t;
				if (t < start)
					start = t;

				if (anim.nodes[index].mirrored)
					anim.pos[index][t] = anim.pos[index][t] * -1.0;

				anim.pos[index][t] *= qc.scale_body_and_sequence; // scale vertex

				clip_rotations(rot);

				anim.rot[index][t] = rot;
			}
		}
		else
		{
			LineScanner retry{currentsmdline};
			std::string cmd;
			if (retry.read_word(cmd))
				retry.read(index);

			if (case_insensitive_compare(cmd, "time"))
			{
				t = index;
			}
			else if (case_insensitive_compare(cmd, "end"))
			{
				anim.startframe = start;
				anim.endframe = end;
				return;
			}
			else
			{
				error("Error(" + std::to_string(smdlinecount) +
					  ") : " + currentsmdline);
			}
		}
	}
	error("unexpected EOF: " + anim.name);
}

static void shift_option_animation(Animation &anim)
{
	if (anim.endframe < anim.startframe)
	{
		error("no animation frames: " + anim.name + "\n");
	}
	for (int j = 0; j < anim.nodes.size(); j++)
	{
		anim.pos[j].erase(anim.pos[j].begin() + anim.endframe + 1, anim.pos[j].end());
		anim.pos[j].erase(anim.pos[j].begin(), anim.pos[j].begin() + anim.startframe);
		anim.rot[j].erase(anim.rot[j].begin() + anim.endframe + 1, anim.rot[j].end());
		anim.rot[j].erase(anim.rot[j].begin(), anim.rot[j].begin() + anim.startframe);
	}
}

void QcCompiler::parse_smd_animation(std::filesystem::path &sequence_smd_path, Animation &anim)
{
	std::filesystem::path smd_path;
	std::string cmd;
	int smd_version;
	smdlinecount = 0;

	if (!case_insensitive_compare(sequence_smd_path.extension().string(), ".smd"))
	{
		sequence_smd_path += ".smd";
	}
	anim.name = sequence_smd_path.stem().string();

	if (sequence_smd_path.is_relative())
	{
		smd_path = (qc.cd / sequence_smd_path).lexically_normal();
	}
	else
	{
		smd_path = sequence_smd_path;
	}

	open_smd(smd_path, "Cannot find \"" + anim.name + ".smd\" in \"" + smd_path.string() + "\"\n");

	logger.info("Grabbing animation: {}", smd_path.string());

	while (smdreader.next(currentsmdline))
	{
		smdlinecount++;
		LineScanner line{currentsmdline};

		if (!line.read_word(cmd))
			continue;
		if (case_insensitive_compare(cmd, "version"))
		{
			if (!line.read(smd_version))
			{
				error("Missing SMD version number.\n");
				return;
			}
			if (smd_version != 1)
			{
				error("Unsupported SMD version: " + std::to_string(smd_version) + "\n");
				return;
			}
		}
		else if (case_insensitive_compare(cmd, "nodes"))
		{
			parse_smd_nodes(anim.nodes);
		}
		else if (case_insensitive_compare(cmd, "skeleton"))
		{
			parse_smd_animation_skeleton(anim);
			shift_option_animation(anim);
		}
	}
	if (anim.nodes.empty() || anim.pos.size() < anim.nodes.size())
	{
		error("missing nodes or skeleton section");
	}
	smd_file.clear();
}

int QcCompiler::cmd_sequence_option_event(std::string &token, Sequence &seq)
{
	if (seq.events.size() >= MAXSTUDIOEVENTS)
	{
		error("Too many events in sequence \"" + seq.name + "\"");
	}

	tokenizer.get_token(false, token);
	int event_id = std::stoi(token);

	tokenizer.get_token(false, token);
	int frame = std::stoi(token);

	seq.events.emplace_back(Event{event_id, frame, ""});

	if (tokenizer.token_available())
	{
		tokenizer.get_token(false, token);
		if (token[0] == '}')
			return 1;
		seq.events.back().options = token;
	}

	return 0;
}

int QcCompiler::cmd_sequence_option_fps(std::string &token, Sequence &seq)
{
	tokenizer.get_token(false, token);
	seq.fps = std::stof(token);

	return 0;
}

void QcCompiler::cmd_origin(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.origin[0] = std::stof(token);

	tokenizer.get_token(false, token);
	qc.origin[1] = std::stof(token);

	tokenizer.get_token(false, token);
	qc.origin[2] = std::stof(token);

	if (tokenizer.token_available())
	{
		tokenizer.get_token(false, token);
		qc.origin_rotation = to_radians(std::stof(token) + ENGINE_ORIENTATION);
	}
}

void QcCompiler::cmd_sequence_option_origin(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.sequence_origin[0] = std::stof(token);

	tokenizer.get_token(false, token);
	qc.sequence_origin[1] = std::stof(token);

	tokenizer.get_token(false, token);
	qc.sequence_origin[2] = std::stof(token);
}

void QcCompiler::cmd_sequence_option_rotate(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.rotate = to_radians(std::stof(token) + ENGINE_ORIENTATION);
}

void QcCompiler::cmd_scale(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.scale = qc.scale_body_and_sequence = std::stof(token);
}

void QcCompiler::cmd_rotate(std::string &token) // XDM
{
	if (!tokenizer.get_token(false, token))
		return;
	qc.rotate = to_radians(std::stof(token) + ENGINE_ORIENTATION);
}

void QcCompiler::cmd_sequence_option_scale(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.scale_body_and_sequence = std::stof(token);
}

static int cmd_sequence_option_action(const std::string &szActivity)
{
	for (int i = 0; activity_map[i].name; i++)
	{
		if (case_insensitive_compare(szActivity, activity_map[i].name))
		{
			return activity_map[i].type;
		}
	}
	if (case_insensitive_n_compare(szActivity, "ACT_", 4))
	{
		return std::stoi(szActivity.substr(4));
	}
	return 0;
}

int QcCompiler::cmd_sequence(std::string &token)
{
	int depth = 0;
	std::vector<std::filesystem::path> smd_files;
	int start = 0;
	int end = MAXSTUDIOANIMATIONS - 1;

	if (!tokenizer.get_token(false, token))
		return 0;

	Sequence newseq{};

	newseq.name = token;

	qc.sequence_origin = qc.origin;
	qc.scale_body_and_sequence = qc.scale;

	qc.rotate = qc.origin_rotation;
	newseq.fps = 30.0;
	newseq.seqgroup = 0; // 0 since $sequencegroup is deprecated
	newseq.blendstart[0] = 0.0;
	newseq.blendend[0] = 1.0;

	while (true)
	{
		if (depth > 0)
		{
			if (!tokenizer.get_token(true, token))
			{
				break;
			}
		}
		else
		{
			if (!tokenizer.token_available())
			{
				break;
			}
			tokenizer.get_token(false, token);
		}

		if (tokenizer.end_of_file())
		{
			if (depth != 0)
			{
				error("Missing '}' in sequence: \"" + newseq.name + "\"");
			}
			return 1;
		}
		if (token == "{")
		{
			depth++;
		}
		else if (token == "}")
		{
			depth--;
		}
		else if (token == "event")
		{
			depth -= cmd_sequence_option_event(token, newseq);
		}
		else if (token == "fps")
		{
			cmd_sequence_option_fps(token, newseq);
		}
		else if (token == "origin")
		{
			cmd_sequence_option_origin(token);
		}
		else if (token == "rotate")
		{
			cmd_sequence_option_rotate(token);
		}
		else if (token == "scale")
		{
			cmd_sequence_option_scale(token);
		}
		else if (token == "loop")
		{
			newseq.flags |= STUDIO_LOOPING;
		}
		else if (token == "frame")
		{
			tokenizer.get_token(false, token);
			start = std::stoi(token);
			tokenizer.get_token(false, token);
			end = std::stoi(token);
		}
		else if (token == "blend")
		{
			tokenizer.get_token(false, token);
			newseq.blendtype[0] = static_cast<float>(lookup_control(token));
			tokenizer.get_token(false, token);
			newseq.blendstart[0] = std::stof(token);
			tokenizer.get_token(false, token);
			newseq.blendend[0] = std::stof(token);
		}
		else if (token == "node")
		{
			tokenizer.get_token(false, token);
			newseq.entrynode = newseq.exitnode = std::stoi(token);
		}
		else if (token == "transition")
		{
			tokenizer.get_token(false, token);
			newseq.entrynode = std::stoi(token);
			tokenizer.get_token(false, token);
			newseq.exitnode = std::stoi(token);
		}
		else if (token == "rtransition")
		{
			tokenizer.get_token(false, token);
			newseq.entrynode = std::stoi(token);
			tokenizer.get_token(false, token);
			newseq.exitnode = std::stoi(token);
			newseq.nodeflags |= 1;
		}
		else if (lookup_control(token) !=
				 -1) // motion flags [motion extraction]
		{
			newseq.motiontype |= lookup_control(token);
		}
		else if (token == "animation")
		{
			tokenizer.get_token(false, token);
			std::replace(token.begin(), token.end(), '\\', '/');
			std::filesystem::path smd_path{token};
			smd_files.push_back(smd_path);
		}
		else if (int i = cmd_sequence_option_action(token); i != 0)
		{
			newseq.activity = i;
			tokenizer.get_token(false, token);
			newseq.actweight = std::stoi(token);
		}
		else
		{
			std::replace(token.begin(), token.end(), '\\', '/');
			std::filesystem::path smd_path{token};
			smd_files.push_back(smd_path);
		}

		if (depth < 0)
		{
			error("Missing '{' in sequence: \"" + newseq.name + "\"");
		}
	};

	if (smd_files.empty())
	{
		error("No animations found in sequence: \"" + newseq.name + "\"");
	}
	for (auto &file : smd_files) //
	{
		Animation &new_anim = newseq.anims.emplace_back();

		// crop the SMD animation from start to end
		new_anim.startframe = start;
		new_anim.endframe = end;

		parse_smd_animation(file, new_anim);
	}

	for (int node : {newseq.entrynode, newseq.exitnode})
	{
		if (node < 0 || node > 100)
		{
			error("sequence \"" + newseq.name + "\": node " + std::to_string(node) + " out of range (0-100)");
		}
	}
	if (newseq.entrynode != newseq.exitnode && (newseq.entrynode == 0 || newseq.exitnode == 0))
	{
		error("sequence \"" + newseq.name + "\": transition to or from node 0");
	}

	qc.sequences.push_back(std::move(newseq));
	return 0;
}

int QcCompiler::cmd_controller(std::string &token)
{
	if (tokenizer.get_token(false, token))
	{
		BoneController newbc{};
		if (token == "mouth")
		{
			newbc.index = 4;
		}
		else
		{
			newbc.index = std::stoi(token);
		}
		if (tokenizer.get_token(false, token))
		{
			newbc.name = token;
			tokenizer.get_token(false, token);
			if ((newbc.type = lookup_control(token)) == -1)
			{
				logger.warning("Unknown bonecontroller type '{}'", token);
				return 0;
			}
			tokenizer.get_token(false, token);
			newbc.start = std::stof(token);
			tokenizer.get_token(false, token);
			newbc.end = std::stof(token);

			if ((newbc.type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR)) &&
				std::abs(newbc.start) < 1.0e6f && std::abs(newbc.end) < 1.0e6f)
			{
				if ((static_cast<int>(newbc.start + 360) % 360) ==
					(static_cast<int>(newbc.end + 360) % 360))
				{
					newbc.type |= STUDIO_RLOOP;
				}
			}
			qc.bonecontrollers.push_back(newbc);
		}
	}
	return 1;
}

void QcCompiler::cmd_bbox(std::string &token)
{ // min
	tokenizer.get_token(false, token);
	qc.bbox[0].x = std::stof(token);

	tokenizer.get_token(false, token);
	qc.bbox[0].y = std::stof(token);

	tokenizer.get_token(false, token);
	qc.bbox[0].z = std::stof(token);
	// max
	tokenizer.get_token(false, token);
	qc.bbox[1].x = std::stof(token);

	tokenizer.get_token(false, token);
	qc.bbox[1].y = std::stof(token);

	tokenizer.get_token(false, token);
	qc.bbox[1].z = std::stof(token);
}

void QcCompiler::cmd_cbox(std::string &token)
{ // min
	tokenizer.get_token(false, token);
	qc.cbox[0].x = std::stof(token);

	tokenizer.get_token(false, token);
	qc.cbox[0].y = std::stof(token);

	tokenizer.get_token(false, token);
	qc.cbox[0].z = std::stof(token);
	// max
	tokenizer.get_token(false, token);
	qc.cbox[1].x = std::stof(token);

	tokenizer.get_token(false, token);
	qc.cbox[1].y = std::stof(token);

	tokenizer.get_token(false, token);
	qc.cbox[1].z = std::stof(token);
}

void QcCompiler::cmd_mirror(std::string &token)
{
	tokenizer.get_token(false, token);
	std::string bonename{token};
	qc.mirroredbones.push_back(bonename);
}

void QcCompiler::cmd_gamma(std::string &token)
{
	tokenizer.get_token(false, token);
	qc.gamma = std::stof(token);
}

int QcCompiler::cmd_texturegroup(std::string &token)
{
	int depth = 0;
	int col_index = 0;
	int row_index = 0;

	if (textures.empty())
		error("Texturegroups must follow model loading\n");

	if (!tokenizer.get_token(false, token))
		return 0;

	if (skinrefcount == 0)
		skinrefcount = textures.size();

	while (true)
	{
		if (!tokenizer.get_token(true, token))
		{
			break;
		}

		if (tokenizer.end_of_file())
		{
			if (depth != 0)
			{
				error("Missing '}' in texturegroup");
			}
			return 1;
		}
		if (token[0] == '{')
		{
			depth++;
		}
		else if (token[0] == '}')
		{
			depth--;
			if (depth == 0)
				break;
			row_index++;
			col_index = 0;
		}
		else if (depth == 2)
		{
			int i = find_texture_index(token);
			qc.texturegroups[row_index][col_index] = i;
			if (row_index != 0)
				textures[i].parent = qc.texturegroups[0][col_index];
			col_index++;
			qc.texturegroup_cols = col_index;
			qc.texturegroup_rows = row_index + 1;
		}
	}

	return 0;
}

int QcCompiler::cmd_hitgroup(std::string &token)
{
	HitGroup newhg{};
	tokenizer.get_token(false, token);
	newhg.group = std::stoi(token);
	tokenizer.get_token(false, token);
	newhg.name = token;
	qc.hitgroups.push_back(newhg);

	return 0;
}

int QcCompiler::cmd_hitbox(std::string &token)
{
	HitBox newhb{};
	tokenizer.get_token(false, token);
	newhb.group = std::stoi(token);
	tokenizer.get_token(false, token);
	newhb.name = token;
	tokenizer.get_token(false, token);
	newhb.bmin.x = std::stof(token);
	tokenizer.get_token(false, token);
	newhb.bmin.y = std::stof(token);
	tokenizer.get_token(false, token);
	newhb.bmin.z = std::stof(token);
	tokenizer.get_token(false, token);
	newhb.bmax.x = std::stof(token);
	tokenizer.get_token(false, token);
	newhb.bmax.y = std::stof(token);
	tokenizer.get_token(false, token);
	newhb.bmax.z = std::stof(token);

	qc.hitboxes.push_back(newhb);

	return 0;
}

int QcCompiler::cmd_attachment(std::string &token)
{
	Attachment newattach{};
	// index
	tokenizer.get_token(false, token); // unused

	// bone name
	tokenizer.get_token(false, token);
	newattach.bonename = token;

	// position
	tokenizer.get_token(false, token);
	newattach.org[0] = std::stof(token);
	tokenizer.get_token(false, token);
	newattach.org[1] = std::stof(token);
	tokenizer.get_token(false, token);
	newattach.org[2] = std::stof(token);

	if (tokenizer.token_available())
		tokenizer.get_token(false, token);

	if (tokenizer.token_available())
		tokenizer.get_token(false, token);

	qc.attachments.push_back(newattach);
	return 0;
}

void QcCompiler::cmd_renamebone(std::string &token)
{
	RenameBone rename{};
	tokenizer.get_token(false, token);
	rename.from = token;
	tokenizer.get_token(false, token);
	rename.to = token;
	qc.renamebones.push_back(rename);
}

void QcCompiler::cmd_texrendermode(std::string &token)
{
	tokenizer.get_token(false, token);
	const std::string tex_name{extension_to_lowercase(token)};

	tokenizer.get_token(false, token);
	if (token == "additive")
	{
		textures[find_texture_index(tex_name)].flags |= STUDIO_NF_ADDITIVE;
	}
	else if (token == "chrome")
	{
		textures[find_texture_index(tex_name)].flags |= STUDIO_NF_CHROME;
	}
	else if (token == "masked")
	{
		textures[find_texture_index(tex_name)].flags |= STUDIO_NF_MASKED;
	}
	else if (token == "fullbright")
	{
		textures[find_texture_index(tex_name)].flags |= STUDIO_NF_FULLBRIGHT;
	}
	else if (token == "flatshade")
	{
		textures[find_texture_index(tex_name)].flags |= STUDIO_NF_FLATSHADE;
	}
	else
		error("Texture \"" + tex_name + "\" has unknown render mode: " + token);
}

void QcCompiler::parse_qc_file(const std::filesystem::path &working_dir)
{
	std::string token;
	while (true)
	{
		// Look for a line starting with a $ command
		while (true)
		{
			tokenizer.get_token(true, token);
			if (tokenizer.end_of_file())
				return;

			if (token[0] == '$')
				break;

			// Skip the rest of the line
			while (tokenizer.token_available())
				tokenizer.get_token(false, token);
		}

		if (token == "$modelname")
		{
			cmd_modelname(token);
		}
		else if (token == "$cd")
		{
			if (!qc.cd.empty())
				error("Two $cd in one model");

			tokenizer.get_token(false, token);
			std::replace(token.begin(), token.end(), '\\', '/');
			const std::filesystem::path cd_path{token};
			if (cd_path.is_relative())
			{
				qc.cd = working_dir / cd_path;
			}
			else
			{
				qc.cd = cd_path;
			}
		}
		else if (token == "$cdtexture")
		{
			if (!qc.cdtexture.empty())
				error("Two $cdtexture in one model");

			tokenizer.get_token(false, token);
			std::replace(token.begin(), token.end(), '\\', '/');
			const std::filesystem::path cdtexture_path{token};
			if (cdtexture_path.is_relative())
			{
				qc.cdtexture = working_dir / cdtexture_path;
			}
			else
			{
				qc.cdtexture = cdtexture_path;
			}
		}
		else if (token == "$scale")
		{
			cmd_scale(token);
		}
		else if (token == "$rotate") // XDM
		{
			cmd_rotate(token);
		}
		else if (token == "$controller")
		{
			cmd_controller(token);
		}
		else if (token == "$body")
		{
			cmd_body(token);
		}
		else if (token == "$bodygroup")
		{
			cmd_bodygroup(token);
		}
		else if (token == "$sequence")
		{
			cmd_sequence(token);
		}
		else if (token == "$eyeposition")
		{
			cmd_eyeposition(token);
		}
		else if (token == "$origin")
		{
			cmd_origin(token);
		}
		else if (token == "$bbox")
		{
			cmd_bbox(token);
		}
		else if (token == "$cbox")
		{
			cmd_cbox(token);
		}
		else if (token == "$mirrorbone")
		{
			cmd_mirror(token);
		}
		else if (token == "$gamma")
		{
			cmd_gamma(token);
		}
		else if (token == "$flags")
		{
			cmd_flags(token);
		}
		else if (token == "$texturegroup")
		{
			cmd_texturegroup(token);
		}
		else if (token == "$hgroup")
		{
			cmd_hitgroup(token);
		}
		else if (token == "$hbox")
		{
			cmd_hitbox(token);
		}
		else if (token == "$attachment")
		{
			cmd_attachment(token);
		}
		else if (token == "$renamebone")
		{
			cmd_renamebone(token);
		}
		else if (token == "$texrendermode")
		{
			cmd_texrendermode(token);
		}
		else
		{
			logger.warning("Incorrect/Unsupported command: {}", token);
		}
	}
}

QcCompiler::QcCompiler(const studiomdl::FileSystem &fs, const studiomdl::CompileOptions &options)
	: CompileState(options), flag_invert_normals(options.invert_normals),
	  flag_keep_all_bones(options.keep_all_bones),
	  normal_blend_cos(std::cos(to_radians(options.normal_blend_angle_deg))), fs(fs)
{
}

void QcCompiler::compile_text(std::string_view qc_path, std::string_view qc_text)
{
	const std::filesystem::path working_dir = std::filesystem::path(qc_path).parent_path();

	tokenizer.load(qc_text);
	logger.info("Processing {}", qc_path);

	// Errors while parsing point at the QC or SMD line being read.
	auto located = [&](const std::string &message)
	{
		if (!smd_file.empty())
			return studiomdl::CompileError(message, smd_file, smdlinecount);
		return studiomdl::CompileError(message, std::string(qc_path), tokenizer.line_number());
	};
	try
	{
		parse_qc_file(working_dir);
	}
	catch (const studiomdl::CompileError &e)
	{
		if (!e.file().empty())
			throw;
		throw located(e.message());
	}
	catch (const std::invalid_argument &)
	{
		throw located("expected a number");
	}
	catch (const std::out_of_range &)
	{
		throw located("number out of range");
	}

	set_skin_values();
	simplify_model();
}

studiomdl::CompileResult studiomdl::compile(std::string_view qc_path, const FileSystem &fs, const CompileOptions &options)
{
	const auto qc_file = FileBytes::load(fs, qc_path);
	if (!qc_file)
	{
		error("Error opening " + std::string(qc_path));
	}
	return compile_source(qc_file->text(),
						  fs, options, qc_path);
}

studiomdl::CompileResult studiomdl::compile_source(std::string_view qc_text, const FileSystem &fs,
													 const CompileOptions &options, std::string_view qc_path)
{
	// On the heap: the compile state holds several large fixed-size tables.
	auto compiler = std::make_unique<QcCompiler>(fs, options);
	compiler->compile_text(qc_path, qc_text);

	CompileResult result;
	result.file_name = mdl_file_name(compiler->qc);
	compiler->logger.info("Writing {}:", result.file_name);
	result.mdl = write_mdl(*compiler);
	return result;
}

void studiomdl::compile_file(const std::filesystem::path &qc_path, const CompileOptions &options)
{
	const std::filesystem::path qc_absolute_path = std::filesystem::absolute(qc_path);
	const CompileResult result = compile(qc_absolute_path.generic_string(), DiskFileSystem{}, options);

	std::unique_ptr<std::ofstream> modelouthandle = safe_open_write(qc_absolute_path.parent_path() / result.file_name);
	safe_write(*modelouthandle, result.mdl.data(), result.mdl.size());
}
