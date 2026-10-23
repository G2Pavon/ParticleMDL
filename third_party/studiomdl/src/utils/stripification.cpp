// tristrip - convert triangle list into tristrips and fans
#include "utils/stripification.hpp"

#include <algorithm>
#include <cstring>

#include "format/mdl.hpp"
#include "modeldata.hpp"
#include "utils/cmdlib.hpp"

// Sizes the work tables for a mesh; they only grow, so later meshes reuse them.
void Stripifier::reserve(int numtris)
{
	const auto n = static_cast<std::size_t>(numtris);
	if (used.size() >= n)
		return;
	used.resize(n);
	// each strip is 1 count + 4 shorts per vertex, at worst one triangle per strip, plus the end marker
	commands.resize(13 * n + 1);
	stripverts.resize(n + 2);
	striptris.resize(n + 2);
	neighbortri.resize(n);
	neighboredge.resize(n);
	bestverts.resize(n + 2);
	besttris.resize(n + 2);
	peak.resize(n);
}

// Orders edges by their vertices (byte-wise, like the memcmp matching), then by triangle and corner.
static int compare_edge_key(const TriangleVert &from_a, const TriangleVert &to_a, const TriangleVert &from_b, const TriangleVert &to_b)
{
	const int c = std::memcmp(&from_a, &from_b, sizeof(TriangleVert));
	return c != 0 ? c : std::memcmp(&to_a, &to_b, sizeof(TriangleVert));
}

void Stripifier::find_neighbor(int starttri, int startv)
{
	// used[starttri] |= (1 << startv);
	TriangleVert *last = &triangles[starttri][0];

	TriangleVert m1 = last[(startv + 1) % 3];
	TriangleVert m2 = last[(startv + 0) % 3];

	// The first later triangle j (and first corner k of it) with edge m1 -> m2,
	// skipping triangles whose three edges are all linked: what a linear scan
	// over j = starttri + 1 .. numtris finds.
	auto it = std::lower_bound(edges.begin(), edges.end(), 0, [&](const Edge &e, int)
							   {
		const int c = compare_edge_key(e.from, e.to, m1, m2);
		return c < 0 || (c == 0 && e.tri <= starttri); });
	for (; it != edges.end() && compare_edge_key(it->from, it->to, m1, m2) == 0; ++it)
	{
		const int j = it->tri;
		const int k = it->vert;
		if (used[j] == 7)
			continue;

		neighbortri[starttri][startv] = j;
		neighboredge[starttri][startv] = k;

		neighbortri[j][k] = starttri;
		neighboredge[j][k] = startv;

		used[starttri] |= (1 << startv);
		used[j] |= (1 << k);
		return;
	}
}

int Stripifier::strip_length(int starttri, int startv)
{
	used[starttri] = 2;

	stripverts[0] = (startv) % 3;
	stripverts[1] = (startv + 1) % 3;
	stripverts[2] = (startv + 2) % 3;

	striptris[0] = starttri;
	striptris[1] = starttri;
	striptris[2] = starttri;
	stripcount = 3;

	while (true)
	{
		int j, k;
		if (stripcount & 1)
		{
			j = neighbortri[starttri][(startv + 1) % 3];
			k = neighboredge[starttri][(startv + 1) % 3];
		}
		else
		{
			j = neighbortri[starttri][(startv + 2) % 3];
			k = neighboredge[starttri][(startv + 2) % 3];
		}
		if (j == -1 || used[j])
			break;

		stripverts[stripcount] = (k + 2) % 3;
		striptris[stripcount] = j;
		stripcount++;

		used[j] = 2;

		starttri = j;
		startv = k;
	}

	// clear the temp used flags: exactly the triangles this strip marked
	for (int j = 0; j < stripcount; j++)
		used[striptris[j]] = 0;

	return stripcount;
}

int Stripifier::fan_length(int starttri, int startv)
{
	used[starttri] = 2;

	stripverts[0] = (startv) % 3;
	stripverts[1] = (startv + 1) % 3;
	stripverts[2] = (startv + 2) % 3;

	striptris[0] = starttri;
	striptris[1] = starttri;
	striptris[2] = starttri;
	stripcount = 3;

	while (true)
	{
		int j = neighbortri[starttri][(startv + 2) % 3];
		int k = neighboredge[starttri][(startv + 2) % 3];

		if (j == -1 || used[j])
			break;

		stripverts[stripcount] = (k + 2) % 3;
		striptris[stripcount] = j;
		stripcount++;

		used[j] = 2;

		starttri = j;
		startv = k;
	}
	// clear the temp used flags: exactly the triangles this strip marked
	for (int j = 0; j < stripcount; j++)
		used[striptris[j]] = 0;

	return stripcount;
}

// Generate a list of trifans or strips for the model, which holds for all frames
int Stripifier::build_tris(Mesh *y, std::uint8_t **ppdata)
{
	int besttype;
	int total = 0;

	pmesh = y;
	triangles = pmesh->triangles.data();
	reserve(pmesh->numtris);

	for (int i = 0; i < pmesh->numtris; i++)
	{
		neighbortri[i][0] = neighbortri[i][1] = neighbortri[i][2] = -1;
		used[i] = 0;
		peak[i] = pmesh->numtris;
	}

	edges.clear();
	for (int i = 0; i < pmesh->numtris; i++)
	{
		for (int k = 0; k < 3; k++)
			edges.push_back({triangles[i][k], triangles[i][(k + 1) % 3], i, k});
	}
	std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b)
			  {
		const int c = compare_edge_key(a.from, a.to, b.from, b.to);
		if (c != 0)
			return c < 0;
		return a.tri != b.tri ? a.tri < b.tri : a.vert < b.vert; });

	// printf("finding neighbors\n");
	for (int i = 0; i < pmesh->numtris; i++)
	{
		for (int k = 0; k < 3; k++)
		{
			if (used[i] & (1 << k))
				continue;

			find_neighbor(i, k);
		}
		// printf("%d", used[i] );
	}
	// printf("\n");

	//
	// build tristrips
	//
	numcommandnodes_ = 0;
	int numcommands = 0;
	std::fill(used.begin(), used.end(), 0);

	for (int i = 0; i < pmesh->numtris;)
	{
		// pick an unused triangle and start the trifan
		if (used[i])
		{
			i++;
			continue;
		}

		int maxlen = 9999;
		int bestlen = 0;
		int m = 0;
		for (int k = i; k < pmesh->numtris && bestlen < 127; k++)
		{
			int localpeak = 0;

			if (used[k])
				continue;

			if (peak[k] <= bestlen)
				continue;

			m++;
			for (int type = 0; type < 2; type++)
			{
				for (int startv = 0; startv < 3; startv++)
				{
					int len;
					if (type == 1)
						len = fan_length(k, startv);
					else
						len = strip_length(k, startv);
					if (len > 127)
					{
						// skip these, they are too long to encode
					}
					else if (len > bestlen)
					{
						besttype = type;
						bestlen = len;
						for (int j = 0; j < bestlen; j++)
						{
							besttris[j] = striptris[j];
							bestverts[j] = stripverts[j];
						}
						// printf("%d %d\n", k, bestlen );
					}
					if (len > localpeak)
						localpeak = len;
				}
			}
			peak[k] = localpeak;
			if (localpeak == maxlen)
				break;
		}
		total += (bestlen - 2);

		// printf("%d (%d) %d\n", bestlen, pmesh->numtris - total, i );

		maxlen = bestlen;

		// mark the tris on the best strip as used
		for (int j = 0; j < bestlen; j++)
			used[besttris[j]] = 1;

		if (besttype == 1)
			commands[numcommands++] = static_cast<short>(-bestlen);
		else
			commands[numcommands++] = static_cast<short>(bestlen);

		for (int j = 0; j < bestlen; j++)
		{
			TriangleVert *tri = &triangles[besttris[j]][bestverts[j]];

			commands[numcommands++] = static_cast<short>(tri->vertindex);
			commands[numcommands++] = static_cast<short>(tri->normindex);
			commands[numcommands++] = static_cast<short>(tri->s);
			commands[numcommands++] = static_cast<short>(tri->t);
		}
		// printf("%d ", bestlen - 2 );
		numcommandnodes_++;
	}

	commands[numcommands++] = 0; // end of list marker

	*ppdata = (std::uint8_t *)commands.data();

	// printf("%d %d %d\n", numcommandnodes_, numcommands, pmesh->numtris  );
	return numcommands * sizeof(short);
}
