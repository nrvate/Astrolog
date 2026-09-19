// tools/proto-float-check.cpp -- the falsification for the 2026-09-18 floats
// drop, kept because it is the thing that measured the defect rather than the
// thing that describes it.
//
//   g++ -O0 -std=gnu++17 -Iephsrv -o /tmp/pfc tools/proto-float-check.cpp && /tmp/pfc
//
// The conformance fixtures (data_inf_*, data_nan_*) are the gate and run in
// ephproto_test. This is the direct form: it builds a DataChunk, encodes it and
// parses it back, so it can ask about a bit pattern no fixture file has to
// exist for. Before the drop every REJECTED line below read ACCEPTED.
//
// Does ParseData accept non-finite values? 3.1: floats MUST be finite, the
// canonical quiet NaN 0x7FF8000000000000 being the one exception "only where
// a field says so".
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include "ephproto.h"
using namespace eph;
static void TryRow(const char *label, double v, uint8_t prec, int fWholeRow) {
  DataChunk d;
  d.chunkIndex = 0; d.iTime = 0; d.nRows = 1; d.totalRows = 1;
  d.precision = prec; d.flags = kChunkLast | kChunkMeta;
  d.columnsPresent = 0; d.nObj = 1;
  d.sources.push_back("swiss");
  Meta m; m.rowsOk = 1; m.errCode = kOErrNone; m.sourceIdx = 0; m.name = "Mars";
  d.meta.push_back(m);
  d.values.assign(6, 1.0);
  if (fWholeRow) d.values.assign(6, v); else d.values[0] = v;
  std::vector<uint8_t> body;
  EncodeData(&body, d);
  DataChunk out; std::string why;
  Outcome o = ParseData(body.data(), body.size(), &out, &why);
  printf("  %-42s -> %s%s\n", label,
    o == kOk ? "ACCEPTED" : "rejected",
    o == kOk ? "" : (" (" + why + ")").c_str());
}
int main() {
  double inf = INFINITY;
  uint64_t sig = 0x7FF8000000000001ull; double nc; memcpy(&nc, &sig, 8);
  uint64_t qn = 0x7FF8000000000000ull; double cn; memcpy(&cn, &qn, 8);
  printf("must be REJECTED:\n");
  TryRow("+Inf in one column", inf, kPrecF64, 0);
  TryRow("-Inf in one column", -inf, kPrecF64, 0);
  TryRow("non-canonical NaN 0x7FF8...0001", nc, kPrecF64, 0);
  TryRow("canonical NaN in ONE column of a finite row", cn, kPrecF64, 0);
  TryRow("+Inf in one column, f32", inf, kPrecF32, 0);
  printf("must be ACCEPTED:\n");
  TryRow("an all-finite row, f64", 1.0, kPrecF64, 1);
  TryRow("an all-finite row, f32", 1.0, kPrecF32, 1);
  TryRow("a whole failed row of canonical NaN, f64", cn, kPrecF64, 1);
  TryRow("a whole failed row of canonical NaN, f32", cn, kPrecF32, 1);
  return 0;
}
