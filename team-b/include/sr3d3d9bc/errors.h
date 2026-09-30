#pragma once

#include <stdexcept>
#include <string>

namespace sr3d3d9bc {

// Thrown only for the one CONFIRMED hard precondition spec-d3d9-sm2-sm3-
// bytecode.md §2 states unconditionally: the blob must be at least 4 bytes
// and its first DWORD's high 16 bits must identify a version token (0xFFFE
// vertex / 0xFFFF pixel). Never thrown for a malformed/truncated/unknown
// interior of the stream - see disassembler.h and DisassembledShader::status
// for how those are reported instead, matching this project's existing
// sr3fxo::ShaderWrapper convention of surfacing partial content rather than
// crashing.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3d3d9bc
