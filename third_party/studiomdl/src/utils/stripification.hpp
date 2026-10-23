#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "modeldata.hpp"

// Converts a mesh's triangle list into GL-style strips and fans.
class Stripifier
{
public:
    // Fills *ppdata with the command list for the mesh's triangles (valid until
    // the next call) and returns its size in bytes.
    int build_tris(Mesh *y, std::uint8_t **ppdata);

    // Number of strips/fans produced by the last build_tris call.
    int numcommandnodes() const { return numcommandnodes_; }

private:
    void reserve(int numtris);
    void find_neighbor(int starttri, int startv);
    int strip_length(int starttri, int startv);
    int fan_length(int starttri, int startv);

    std::vector<int> used;
    // the command list holds counts and s/t values that are valid for
    // every frame
    std::vector<short> commands;
    // all frames will have their vertexes rearranged and expanded
    // so they are in the order expected by the command list
    std::vector<int> stripverts;
    std::vector<int> striptris;
    int stripcount = 0;
    std::vector<std::array<int, 3>> neighbortri;
    std::vector<std::array<int, 3>> neighboredge;
    std::vector<int> bestverts;
    std::vector<int> besttris;
    std::vector<int> peak;

    // Directed edge vert -> vert + 1 of triangle `tri`, sorted for neighbor lookup.
    struct Edge
    {
        TriangleVert from, to;
        int tri, vert;
    };
    std::vector<Edge> edges;

    std::array<TriangleVert, 3> *triangles = nullptr;
    Mesh *pmesh = nullptr;
    int numcommandnodes_ = 0;
};
