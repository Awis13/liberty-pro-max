#pragma once

// Call before any Metal use, including preparation commands. The private helper
// and guard failures return >= 0; ordinary invocations return -1 with the guard held.
int GtaivMetalArchiverSession(int argc, char** argv);
