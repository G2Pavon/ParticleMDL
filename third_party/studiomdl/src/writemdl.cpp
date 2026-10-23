#include "writemdl.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "format/mdl.hpp"
#include "format/qc.hpp"
#include "studiomdl.hpp"
#include "utils/mathlib.hpp"
#include "utils/stripification.hpp"
#include "utils/cmdlib.hpp"

constexpr int FILEBUFFER = 16 * 1024 * 1024;

#define ALIGN(a) (((uintptr_t)(a) + 3) & ~(uintptr_t)3)

namespace
{
class MdlWriter
{
public:
	explicit MdlWriter(CompileState &compile_state) : state(compile_state), qc(compile_state.qc) {}

	std::vector<std::uint8_t> write();

private:
	void write_bone_info(StudioHeader *header);
	void write_sequence_info(StudioHeader *header, int &frames, float &seconds);
	std::uint8_t *write_animations(std::uint8_t *pData, const std::uint8_t *pStart, int group);
	void write_textures(StudioHeader *header);
	void write_model(StudioHeader *header);

	// Upper bound on the .mdl size, computed from data known before writing.
	std::size_t size_bound() const;

	// Fails if `bytes` (plus alignment) do not fit in the output at `at`;
	// otherwise grows the output over them, zero-filled. Every region is
	// claimed before it is written, and growth stays within the capacity
	// reserved from size_bound(), so pointers into the output stay valid.
	void claim(const std::uint8_t *at, std::size_t bytes);
	void claim(std::size_t bytes) { claim(currentposition, bytes); }

	// Copies a name into a fixed-size, zero-filled field, truncating with a warning.
	template <std::size_t N>
	void copy_name(char (&dest)[N], const std::string &name, const char *what) const
	{
		if (name.size() >= N)
			state.logger.warning("{} name \"{}\" is longer than {} characters; truncated", what, name, N - 1);
		std::strncpy(dest, name.c_str(), N - 1);
	}

	CompileState &state;
	QC &qc;
	Stripifier stripifier;
	std::vector<std::uint8_t> buffer; // the .mdl being built; never reallocated
	std::uint8_t *currentposition = nullptr;
	std::uint8_t *bufferstart = nullptr;
};
} // namespace

void MdlWriter::claim(const std::uint8_t *at, std::size_t bytes)
{
	const std::size_t needed = static_cast<std::size_t>(at - bufferstart) + bytes + 4;
	if (needed > FILEBUFFER)
	{
		error("model too large: the .mdl would exceed " + std::to_string(FILEBUFFER / (1024 * 1024)) + " MB");
	}
	if (needed > buffer.capacity())
	{
		throw std::logic_error("internal error: .mdl size bound too small");
	}
	if (needed > buffer.size())
	{
		buffer.resize(needed); // zero-fills; within capacity, so no reallocation
	}
}

std::size_t MdlWriter::size_bound() const
{
	constexpr std::size_t pad = 8; // per region: claim slack (4) and alignment (up to 3)
	const std::size_t bones = state.bonetable.size();

	std::size_t n = sizeof(StudioHeader) + pad;
	n += bones * sizeof(StudioBone) + pad;
	n += qc.bonecontrollers.size() * sizeof(StudioBoneController) + pad;
	n += qc.attachments.size() * sizeof(StudioAttachment) + pad;
	n += qc.hitboxes.size() * sizeof(StudioHitbox) + pad;

	for (const auto &sequence : qc.sequences)
	{
		n += sequence.anims.size() * bones * sizeof(StudioAnimationFrameOffset) + pad;
		for (const auto &anim : sequence.anims)
		{
			for (const auto &bone : anim.anims)
			{
				for (const auto &track : bone)
					n += track.size() * sizeof(StudioAnimationValue);
				n += pad;
			}
		}
		n += sequence.events.size() * sizeof(StudioAnimationEvent) + 2 * pad;
	}
	n += qc.sequences.size() * sizeof(StudioSequenceDescription) + pad;
	n += sizeof(StudioSequenceGroup) + pad;
	n += static_cast<std::size_t>(state.numxnodes) * state.numxnodes + pad;

	n += qc.bodyparts.size() * sizeof(StudioBodyPart) + qc.submodels.size() * sizeof(StudioModel) + pad;
	for (const auto &model : qc.submodels)
	{
		const std::size_t points = model->verts.size() + model->normals.size();
		n += points + 2 * pad;                   // vertex and normal bone indices
		n += points * sizeof(Vector3) + 2 * pad; // vertices and normals
		n += model->nummesh * sizeof(StudioMesh) + pad;
		for (int j = 0; j < model->nummesh; j++) // strip commands, worst case one triangle per strip
			n += (13 * static_cast<std::size_t>(model->pmeshes[j]->numtris) + 1) * sizeof(short) + pad;
	}

	n += state.textures.size() * sizeof(StudioTexture) + pad;
	n += static_cast<std::size_t>(std::max(state.skinfamiliescount, 0)) * std::max(state.skinrefcount, 0) * sizeof(short) + pad;
	for (const auto &texture : state.textures)
		n += texture.size + pad;
	return n;
}

std::string mdl_file_name(const QC &qc)
{
	return strip_extension(qc.modelname) + ".mdl";
}

void MdlWriter::write_bone_info(StudioHeader *header)
{
	// save bone info
	claim(state.bonetable.size() * sizeof(StudioBone));
	StudioBone *pbone = (StudioBone *)currentposition;
	header->numbones = state.bonetable.size();
	header->boneindex = static_cast<int>(currentposition - bufferstart);

	for (int i = 0; i < state.bonetable.size(); i++)
	{
		copy_name(pbone[i].name, state.bonetable[i].name, "bone");
		pbone[i].parent = state.bonetable[i].parent;
		pbone[i].value[0] = state.bonetable[i].pos[0];
		pbone[i].value[1] = state.bonetable[i].pos[1];
		pbone[i].value[2] = state.bonetable[i].pos[2];
		pbone[i].value[3] = state.bonetable[i].rot[0];
		pbone[i].value[4] = state.bonetable[i].rot[1];
		pbone[i].value[5] = state.bonetable[i].rot[2];
		pbone[i].scale[0] = state.bonetable[i].posscale[0];
		pbone[i].scale[1] = state.bonetable[i].posscale[1];
		pbone[i].scale[2] = state.bonetable[i].posscale[2];
		pbone[i].scale[3] = state.bonetable[i].rotscale[0];
		pbone[i].scale[4] = state.bonetable[i].rotscale[1];
		pbone[i].scale[5] = state.bonetable[i].rotscale[2];
	}
	currentposition += state.bonetable.size() * sizeof(StudioBone);
	currentposition = (std::uint8_t *)ALIGN(currentposition);

	// map bonecontroller to bones
	for (int i = 0; i < state.bonetable.size(); i++)
	{
		for (int j = 0; j < DEGREESOFFREEDOM; j++)
		{
			pbone[i].bonecontroller[j] = -1;
		}
	}

	for (int i = 0; i < qc.bonecontrollers.size(); i++)
	{
		int j = qc.bonecontrollers[i].bone;
		switch (qc.bonecontrollers[i].type & STUDIO_TYPES)
		{
		case STUDIO_X:
			pbone[j].bonecontroller[0] = i;
			break;
		case STUDIO_Y:
			pbone[j].bonecontroller[1] = i;
			break;
		case STUDIO_Z:
			pbone[j].bonecontroller[2] = i;
			break;
		case STUDIO_XR:
			pbone[j].bonecontroller[3] = i;
			break;
		case STUDIO_YR:
			pbone[j].bonecontroller[4] = i;
			break;
		case STUDIO_ZR:
			pbone[j].bonecontroller[5] = i;
			break;
		default:
			error("Unknown bonecontroller type");
		}
	}

	// save bonecontroller info
	claim(qc.bonecontrollers.size() * sizeof(StudioBoneController));
	StudioBoneController *pbonecontroller = (StudioBoneController *)currentposition;
	header->numbonecontrollers = qc.bonecontrollers.size();
	header->bonecontrollerindex = static_cast<int>(currentposition - bufferstart);

	for (int i = 0; i < qc.bonecontrollers.size(); i++)
	{
		pbonecontroller[i].bone = qc.bonecontrollers[i].bone;
		pbonecontroller[i].index = qc.bonecontrollers[i].index;
		pbonecontroller[i].type = qc.bonecontrollers[i].type;
		pbonecontroller[i].start = qc.bonecontrollers[i].start;
		pbonecontroller[i].end = qc.bonecontrollers[i].end;
	}
	currentposition += qc.bonecontrollers.size() * sizeof(StudioBoneController);
	currentposition = (std::uint8_t *)ALIGN(currentposition);

	// save attachment info
	claim(qc.attachments.size() * sizeof(StudioAttachment));
	StudioAttachment *pattachment = (StudioAttachment *)currentposition;
	header->numattachments = qc.attachments.size();
	header->attachmentindex = static_cast<int>(currentposition - bufferstart);

	for (int i = 0; i < qc.attachments.size(); i++)
	{
		pattachment[i].bone = qc.attachments[i].bone;
		pattachment[i].org = qc.attachments[i].org;
	}
	currentposition += qc.attachments.size() * sizeof(StudioAttachment);
	currentposition = (std::uint8_t *)ALIGN(currentposition);

	// save bbox info
	claim(qc.hitboxes.size() * sizeof(StudioHitbox));
	StudioHitbox *pbbox = (StudioHitbox *)currentposition;
	header->numhitboxes = qc.hitboxes.size();
	header->hitboxindex = static_cast<int>(currentposition - bufferstart);

	for (int i = 0; i < qc.hitboxes.size(); i++)
	{
		pbbox[i].bone = qc.hitboxes[i].bone;
		pbbox[i].group = qc.hitboxes[i].group;
		pbbox[i].bbmin = qc.hitboxes[i].bmin;
		pbbox[i].bbmax = qc.hitboxes[i].bmax;
	}
	currentposition += qc.hitboxes.size() * sizeof(StudioHitbox);
	currentposition = (std::uint8_t *)ALIGN(currentposition);
}
void MdlWriter::write_sequence_info(StudioHeader *header, int &frames, float &seconds)
{
	// save sequence info
	claim(qc.sequences.size() * sizeof(StudioSequenceDescription));
	StudioSequenceDescription *pseqdesc = (StudioSequenceDescription *)currentposition;
	header->numseq = qc.sequences.size();
	header->seqindex = static_cast<int>(currentposition - bufferstart);
	currentposition += qc.sequences.size() * sizeof(StudioSequenceDescription);

	for (int i = 0; i < qc.sequences.size(); i++, pseqdesc++)
	{
		copy_name(pseqdesc->label, qc.sequences[i].name, "sequence");
		pseqdesc->numframes = qc.sequences[i].numframes;
		pseqdesc->fps = qc.sequences[i].fps;
		pseqdesc->flags = qc.sequences[i].flags;

		pseqdesc->numblends = qc.sequences[i].anims.size();

		pseqdesc->blendtype[0] = static_cast<int>(qc.sequences[i].blendtype[0]);
		pseqdesc->blendtype[1] = static_cast<int>(qc.sequences[i].blendtype[1]);
		pseqdesc->blendstart[0] = qc.sequences[i].blendstart[0];
		pseqdesc->blendend[0] = qc.sequences[i].blendend[0];
		pseqdesc->blendstart[1] = qc.sequences[i].blendstart[1];
		pseqdesc->blendend[1] = qc.sequences[i].blendend[1];

		pseqdesc->motiontype = qc.sequences[i].motiontype;
		pseqdesc->motionbone = 0; // sequence[i].motionbone;
		pseqdesc->linearmovement = qc.sequences[i].linearmovement;

		pseqdesc->seqgroup = qc.sequences[i].seqgroup;

		pseqdesc->animindex = qc.sequences[i].animindex;

		pseqdesc->activity = qc.sequences[i].activity;
		pseqdesc->actweight = qc.sequences[i].actweight;

		pseqdesc->bbmin = qc.sequences[i].bmin;
		pseqdesc->bbmax = qc.sequences[i].bmax;

		pseqdesc->entrynode = qc.sequences[i].entrynode;
		pseqdesc->exitnode = qc.sequences[i].exitnode;
		pseqdesc->nodeflags = qc.sequences[i].nodeflags;

		frames += qc.sequences[i].numframes;
		if (qc.sequences[i].fps > 0) // statistics only; fps 0 is stored as given
			seconds += static_cast<float>(qc.sequences[i].numframes) / qc.sequences[i].fps;

		// save events
		{
			claim(qc.sequences[i].events.size() * sizeof(StudioAnimationEvent));
			StudioAnimationEvent *pevent = (StudioAnimationEvent *)currentposition; // Declare inside the block
			pseqdesc->numevents = qc.sequences[i].events.size();
			pseqdesc->eventindex = static_cast<int>(currentposition - bufferstart);
			currentposition += pseqdesc->numevents * sizeof(StudioAnimationEvent);
			for (int j = 0; j < qc.sequences[i].events.size(); j++)
			{
				pevent[j].frame = qc.sequences[i].events[j].frame - qc.sequences[i].frameoffset;
				pevent[j].event = qc.sequences[i].events[j].event;
				std::strncpy(pevent[j].options, qc.sequences[i].events[j].options.c_str(), sizeof(pevent[j].options) - 1);
			}
			currentposition = (std::uint8_t *)ALIGN(currentposition);
		}
	}

	// save sequence group info
	claim(sizeof(StudioSequenceGroup));
	StudioSequenceGroup *pseqgroup = (StudioSequenceGroup *)currentposition;
	header->numseqgroups = 1; // 1 since $sequencegroup is deprecated
	header->seqgroupindex = static_cast<int>(currentposition - bufferstart);
	currentposition += header->numseqgroups * sizeof(StudioSequenceGroup);
	currentposition = (std::uint8_t *)ALIGN(currentposition);
	std::strcpy(pseqgroup[0].label, "default");
	std::strcpy(pseqgroup[0].name, "");

	// save transition graph
	claim(static_cast<std::size_t>(state.numxnodes) * state.numxnodes);
	std::uint8_t *ptransition = (std::uint8_t *)currentposition;
	header->numtransitions = state.numxnodes;
	header->transitionindex = static_cast<int>(currentposition - bufferstart);
	currentposition += state.numxnodes * state.numxnodes * sizeof(std::uint8_t);
	currentposition = (std::uint8_t *)ALIGN(currentposition);
	for (int i = 0; i < state.numxnodes; i++)
	{
		for (int j = 0; j < state.numxnodes; j++)
		{
			*ptransition++ = static_cast<std::uint8_t>(state.xnode[i][j]);
		}
	}
}

std::uint8_t *MdlWriter::write_animations(std::uint8_t *pData, const std::uint8_t *pStart, int group)
{
	// hack for seqgroup 0
	// pseqgroup->data = (pData - pStart);

	for (int i = 0; i < qc.sequences.size(); i++)
	{
		if (qc.sequences[i].seqgroup == group)
		{
			// save animations
			claim(pData, qc.sequences[i].anims.size() * state.bonetable.size() * sizeof(StudioAnimationFrameOffset));
			StudioAnimationFrameOffset *panim = (StudioAnimationFrameOffset *)pData;
			qc.sequences[i].animindex = static_cast<int>(pData - pStart);
			pData += qc.sequences[i].anims.size() * state.bonetable.size() * sizeof(StudioAnimationFrameOffset);
			pData = (std::uint8_t *)ALIGN(pData);

			StudioAnimationValue *panimvalue = (StudioAnimationValue *)pData;
			for (int blends = 0; blends < qc.sequences[i].anims.size(); blends++)
			{
				// save animation value info
				for (int j = 0; j < state.bonetable.size(); j++)
				{
					std::size_t values = 0;
					for (const auto &track : qc.sequences[i].anims[blends].anims[j])
						values += track.size();
					claim((std::uint8_t *)panimvalue, values * sizeof(StudioAnimationValue));
					for (int k = 0; k < DEGREESOFFREEDOM; k++)
					{
						if (qc.sequences[i].anims[blends].anims[j][k].empty())
						{
							panim->offset[k] = 0;
						}
						else
						{
							panim->offset[k] = static_cast<std::uint16_t>((std::uint8_t *)panimvalue - (std::uint8_t *)panim);
							for (std::size_t n = 0; n < qc.sequences[i].anims[blends].anims[j][k].size(); n++)
							{
								panimvalue->value = qc.sequences[i].anims[blends].anims[j][k][n].value;
								panimvalue++;
							}
						}
					}
					if (((std::uint8_t *)panimvalue - (std::uint8_t *)panim) > 65535)
						error("sequence " + qc.sequences[i].name + " is greate than 64K\n");
					panim++;
				}
			}

			// printf("raw bone data %d : %s\n", (std::uint8_t *)panimvalue - pData, sequence[i].name);
			pData = (std::uint8_t *)panimvalue;
			pData = (std::uint8_t *)ALIGN(pData);
		}
	}
	return pData;
}

void MdlWriter::write_textures(StudioHeader *header)
{
	// save bone info
	claim(state.textures.size() * sizeof(StudioTexture));
	StudioTexture *ptexture = (StudioTexture *)currentposition;
	header->numtextures = state.textures.size();
	header->textureindex = static_cast<int>(currentposition - bufferstart);
	currentposition += state.textures.size() * sizeof(StudioTexture);
	currentposition = (std::uint8_t *)ALIGN(currentposition);

	header->skinindex = static_cast<int>(currentposition - bufferstart);
	header->numskinref = state.skinrefcount;
	header->numskinfamilies = state.skinfamiliescount;
	claim(static_cast<std::size_t>(header->numskinfamilies) * header->numskinref * sizeof(short));
	short *pref = (short *)currentposition;

	for (int i = 0; i < header->numskinfamilies; i++)
	{
		for (int j = 0; j < header->numskinref; j++)
		{
			*pref = static_cast<short>(state.skinref[i][j]);
			pref++;
		}
	}
	currentposition = (std::uint8_t *)pref;
	currentposition = (std::uint8_t *)ALIGN(currentposition);

	header->texturedataindex = static_cast<int>(currentposition - bufferstart); // must be the end of the file!

	for (int i = 0; i < state.textures.size(); i++)
	{
		copy_name(ptexture[i].name, state.textures[i].name, "texture");
		ptexture[i].flags = state.textures[i].flags;
		ptexture[i].width = state.textures[i].skinwidth;
		ptexture[i].height = state.textures[i].skinheight;
		ptexture[i].index = static_cast<int>(currentposition - bufferstart);
		claim(state.textures[i].size);
		write_skin(state.textures[i], currentposition);
		currentposition += state.textures[i].size;
	}
	currentposition = (std::uint8_t *)ALIGN(currentposition);
}

void MdlWriter::write_model(StudioHeader *header)
{
	claim(qc.bodyparts.size() * sizeof(StudioBodyPart) + qc.submodels.size() * sizeof(StudioModel));
	StudioBodyPart *pbodypart = (StudioBodyPart *)currentposition;
	header->numbodyparts = qc.bodyparts.size();
	header->bodypartindex = static_cast<int>(currentposition - bufferstart);
	currentposition += qc.bodyparts.size() * sizeof(StudioBodyPart);

	StudioModel *pmodel = (StudioModel *)currentposition;
	currentposition += qc.submodels.size() * sizeof(StudioModel);

	for (int i = 0, j = 0; i < qc.bodyparts.size(); i++)
	{
		copy_name(pbodypart[i].name, qc.bodyparts[i].name, "bodygroup");
		pbodypart[i].nummodels = qc.bodyparts[i].num_submodels;
		pbodypart[i].base = qc.bodyparts[i].base;
		pbodypart[i].modelindex = static_cast<int>((std::uint8_t *)&pmodel[j] - bufferstart);
		j += qc.bodyparts[i].num_submodels;
	}
	currentposition = (std::uint8_t *)ALIGN(currentposition);

	const std::uint8_t *cur = currentposition;
	for (int i = 0; i < qc.submodels.size(); i++)
	{
		int normmap[MAXSTUDIOVERTS];
		int normimap[MAXSTUDIOVERTS];
		int n = 0;

		copy_name(pmodel[i].name, qc.submodels[i]->name, "model");

		// save bbox info

		// remap normals to be sorted by skin reference
		for (int j = 0; j < qc.submodels[i]->nummesh; j++)
		{
			for (int k = 0; k < qc.submodels[i]->normals.size(); k++)
			{
				if (qc.submodels[i]->normals[k].skinref == qc.submodels[i]->pmeshes[j]->skinref)
				{
					normmap[k] = n;
					normimap[n] = k;
					n++;
					qc.submodels[i]->pmeshes[j]->numnorms++;
				}
			}
		}

		// save vertice bones
		claim(qc.submodels[i]->verts.size() + qc.submodels[i]->normals.size() + 4);
		std::uint8_t *pbone = currentposition;
		pmodel[i].numverts = qc.submodels[i]->verts.size();
		pmodel[i].vertinfoindex = static_cast<int>(currentposition - bufferstart);
		for (int j = 0; j < pmodel[i].numverts; j++)
		{
			*pbone++ = static_cast<std::uint8_t>(qc.submodels[i]->verts[j].bone_id);
		}
		pbone = (std::uint8_t *)ALIGN(pbone);

		// save normal bones
		pmodel[i].numnorms = qc.submodels[i]->normals.size();
		pmodel[i].norminfoindex = static_cast<int>((std::uint8_t *)pbone - bufferstart);
		for (int j = 0; j < pmodel[i].numnorms; j++)
		{
			*pbone++ = static_cast<std::uint8_t>(qc.submodels[i]->normals[normimap[j]].bone_id);
		}
		pbone = (std::uint8_t *)ALIGN(pbone);

		currentposition = pbone;

		// save group info
		{
			claim((qc.submodels[i]->verts.size() + qc.submodels[i]->normals.size()) * sizeof(Vector3) + 4);
			Vector3 *pvert = (Vector3 *)currentposition;
			currentposition += qc.submodels[i]->verts.size() * sizeof(Vector3);
			pmodel[i].vertindex = static_cast<int>((std::uint8_t *)pvert - bufferstart);
			currentposition = (std::uint8_t *)ALIGN(currentposition);

			Vector3 *pnorm = (Vector3 *)currentposition;
			currentposition += qc.submodels[i]->normals.size() * sizeof(Vector3);
			pmodel[i].normindex = static_cast<int>((std::uint8_t *)pnorm - bufferstart);
			currentposition = (std::uint8_t *)ALIGN(currentposition);

			for (int j = 0; j < qc.submodels[i]->verts.size(); j++)
			{
				pvert[j] = qc.submodels[i]->verts[j].pos;
			}

			for (int j = 0; j < qc.submodels[i]->normals.size(); j++)
			{
				pnorm[j] = qc.submodels[i]->normals[normimap[j]].pos;
			}
			state.logger.info("vertices  {:6} bytes ({} vertices, {} normals)", currentposition - cur, qc.submodels[i]->verts.size(), qc.submodels[i]->normals.size());
			cur = currentposition;
		}
		// save mesh info
		{
			claim(qc.submodels[i]->nummesh * sizeof(StudioMesh));
			StudioMesh *pmesh = (StudioMesh *)currentposition;
			pmodel[i].nummesh = qc.submodels[i]->nummesh;
			pmodel[i].meshindex = static_cast<int>(currentposition - bufferstart);
			currentposition += pmodel[i].nummesh * sizeof(StudioMesh);
			currentposition = (std::uint8_t *)ALIGN(currentposition);

			int total_tris = 0;
			int total_strips = 0;
			for (int j = 0; j < qc.submodels[i]->nummesh; j++)
			{
				std::uint8_t *pCmdSrc;

				pmesh[j].numtris = qc.submodels[i]->pmeshes[j]->numtris;
				pmesh[j].skinref = qc.submodels[i]->pmeshes[j]->skinref;
				pmesh[j].numnorms = qc.submodels[i]->pmeshes[j]->numnorms;

				for (int k = 0; k < pmesh[j].numtris; k++)
				{
					for (TriangleVert &vert : qc.submodels[i]->pmeshes[j]->triangles[k])
						vert.normindex = normmap[vert.normindex];
				}

				int numCmdBytes = stripifier.build_tris(qc.submodels[i]->pmeshes[j].get(), &pCmdSrc);

				pmesh[j].triindex = static_cast<int>(currentposition - bufferstart);
				claim(numCmdBytes);
				memcpy(currentposition, pCmdSrc, numCmdBytes);
				currentposition += numCmdBytes;
				currentposition = (std::uint8_t *)ALIGN(currentposition);
				total_tris += pmesh[j].numtris;
				total_strips += stripifier.numcommandnodes();
			}
			state.logger.info("mesh      {:6} bytes ({} tris, {} strips)", currentposition - cur, total_tris, total_strips);
			cur = currentposition;
		}
	}
}

std::vector<std::uint8_t> MdlWriter::write()
{
	int total = 0;

	buffer.clear();
	buffer.reserve(std::min<std::size_t>(size_bound(), FILEBUFFER));
	bufferstart = buffer.data();
	currentposition = bufferstart;
	claim(sizeof(StudioHeader));

	std::string file_name = mdl_file_name(qc);

	StudioHeader *studioheader = (StudioHeader *)bufferstart;

	studioheader->ident = IDSTUDIOHEADER;
	studioheader->version = STUDIO_VERSION;
	copy_name(studioheader->name, file_name, "model file");

	studioheader->eyeposition = qc.eyeposition;
	studioheader->min = qc.bbox[0];
	studioheader->max = qc.bbox[1];
	studioheader->bbmin = qc.cbox[0];
	studioheader->bbmax = qc.cbox[1];

	studioheader->flags = qc.flags;

	currentposition = (std::uint8_t *)studioheader + sizeof(StudioHeader);

	write_bone_info(studioheader);
	state.logger.info("bones     {:6} bytes ({})", currentposition - bufferstart - total, state.bonetable.size());
	total = static_cast<int>(currentposition - bufferstart);

	currentposition = write_animations(currentposition, bufferstart, 0);

	int total_frames = 0;
	float total_seconds = 0;
	write_sequence_info(studioheader, total_frames, total_seconds);
	state.logger.info("sequences {:6} bytes ({} frames) [{}:{:02}]", currentposition - bufferstart - total, total_frames, static_cast<int>(total_seconds) / 60, static_cast<int>(total_seconds) % 60);
	total = static_cast<int>(currentposition - bufferstart);

	write_model(studioheader);
	state.logger.info("models    {:6} bytes", currentposition - bufferstart - total);
	total = static_cast<int>(currentposition - bufferstart);

	write_textures(studioheader);
	state.logger.info("textures  {:6} bytes", currentposition - bufferstart - total);

	studioheader->length = static_cast<int>(currentposition - bufferstart);

	state.logger.info("total     {:6}", studioheader->length);

	buffer.resize(studioheader->length);
	return std::move(buffer);
}

std::vector<std::uint8_t> write_mdl(CompileState &state)
{
	return MdlWriter{state}.write();
}
