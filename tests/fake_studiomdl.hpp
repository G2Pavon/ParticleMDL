#pragma once

// What the test executable does when it is started as "<it> <name>.qc" inside a
// folder. The first line of the .qc picks the behavior:
//   ok         writes <name>.mdl, prints to stdout and stderr, exits 0
//   fail       prints an error, exits 1
//   nomdl      exits 0 without writing the .mdl
//   sleep      waits 30 s, then writes <name>.mdl (to be killed before)
//   cwd        prints its current folder
//   stdin      reads stdin to the end and prints how many bytes there were
//   lots       prints about 2 MB
//   args       prints its single argument as it received it
int fake_studiomdl(const char* qc_name);
