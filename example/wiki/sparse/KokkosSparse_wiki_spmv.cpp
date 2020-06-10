#include "Kokkos_Core.hpp"

#include "KokkosKernels_default_types.hpp"
#include "KokkosSparse_CrsMatrix.hpp"
#include "KokkosSparse_spmv.hpp"

using Scalar  = default_scalar;
using Ordinal = default_lno_t;
using Offset  = default_size_type;
using Layout  = default_layout;

template <class Yvector>
struct check_spmv_functor {
  Yvector y;
  const Scalar SC_ONE = Kokkos::ArithTraits<Scalar>::one();

  check_spmv_functor(Yvector y_) : y(y_){};

  KOKKOS_INLINE_FUNCTION
  void operator()(const int i, Ordinal& update) const {
    if (y(i) != (SC_ONE + SC_ONE)) {
      ++update;
    }
  }
};

int main(int argc, char* argv[]) {
  Kokkos::initialize();

  using device_type = typename Kokkos::Device<
      Kokkos::DefaultExecutionSpace,
      typename Kokkos::DefaultExecutionSpace::memory_space>;
  using matrix_type =
      typename KokkosSparse::CrsMatrix<Scalar, Ordinal, device_type, void,
                                       Offset>;
  using graph_type   = typename matrix_type::staticcrsgraph_type;
  using row_map_type = typename graph_type::row_map_type;
  using entries_type = typename graph_type::entries_type;
  using values_type  = typename matrix_type::values_type;

  int return_value = 0;

  {
    const Scalar SC_ONE = Kokkos::ArithTraits<Scalar>::one();

    Ordinal numRows = 10;

    // Build the row pointers and store numNNZ
    typename row_map_type::non_const_type row_map("row pointers", numRows + 1);
    typename row_map_type::HostMirror row_map_h =
        Kokkos::create_mirror_view(row_map);
    for (Ordinal rowIdx = 1; rowIdx < numRows + 1; ++rowIdx) {
      if ((rowIdx == 1) || (rowIdx == numRows)) {
        row_map_h(rowIdx) = row_map_h(rowIdx - 1) + 2;
      } else {
        row_map_h(rowIdx) = row_map_h(rowIdx - 1) + 3;
      }
    }
    const Offset numNNZ = row_map_h(numRows);
    Kokkos::deep_copy(row_map, row_map_h);

    typename entries_type::non_const_type entries("column indices", numNNZ);
    typename entries_type::HostMirror entries_h =
        Kokkos::create_mirror_view(entries);
    typename values_type::non_const_type values("values", numNNZ);
    typename values_type::HostMirror values_h =
        Kokkos::create_mirror_view(values);
    for (Ordinal rowIdx = 0; rowIdx < numRows; ++rowIdx) {
      if (rowIdx == 0) {
        entries_h(0) = rowIdx;
        entries_h(1) = rowIdx + 1;

        values_h(0) = SC_ONE;
        values_h(1) = -SC_ONE;
      } else if (rowIdx == numRows - 1) {
        entries_h(row_map_h(rowIdx))     = rowIdx - 1;
        entries_h(row_map_h(rowIdx) + 1) = rowIdx;

        values_h(row_map_h(rowIdx))     = -SC_ONE;
        values_h(row_map_h(rowIdx) + 1) = SC_ONE;
      } else {
        entries_h(row_map_h(rowIdx))     = rowIdx - 1;
        entries_h(row_map_h(rowIdx) + 1) = rowIdx;
        entries_h(row_map_h(rowIdx) + 2) = rowIdx + 1;

        values_h(row_map_h(rowIdx))     = -SC_ONE;
        values_h(row_map_h(rowIdx) + 1) = SC_ONE + SC_ONE;
        values_h(row_map_h(rowIdx) + 2) = -SC_ONE;
      }
    }
    Kokkos::deep_copy(entries, entries_h);
    Kokkos::deep_copy(values, values_h);

    graph_type myGraph(entries, row_map);
    matrix_type myMatrix("test matrix", numRows, values, myGraph);

    const Scalar alpha = SC_ONE;
    const Scalar beta  = SC_ONE;

    typename values_type::non_const_type x("lhs", numRows);
    typename values_type::non_const_type y("rhs", numRows);
    Kokkos::deep_copy(x, SC_ONE);
    Kokkos::deep_copy(y, SC_ONE + SC_ONE);

    KokkosSparse::spmv("N", alpha, myMatrix, x, beta, y);

    Ordinal count_errors = 0;
    check_spmv_functor<values_type> check_spmv(y);
    Kokkos::parallel_reduce(Kokkos::RangePolicy<Ordinal>(0, numRows),
                            check_spmv, count_errors);
    if (count_errors > 0) {
      return_value = 1;
      std::cout << "Found " << count_errors << " errors in y vector!"
                << std::endl;
    } else {
      std::cout << "spmv was performed correctly: y = beta*y + alpha*A*x"
                << std::endl;
    }
  }

  Kokkos::finalize();

  return return_value;
}