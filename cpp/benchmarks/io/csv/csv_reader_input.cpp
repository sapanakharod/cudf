/*
 * Copyright (c) 2022, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <benchmarks/common/generate_input.hpp>
#include <benchmarks/fixture/benchmark_fixture.hpp>
#include <benchmarks/fixture/rmm_pool_raii.hpp>
#include <benchmarks/io/cuio_common.hpp>
#include <benchmarks/io/nvbench_helpers.hpp>

#include <cudf/io/csv.hpp>
#include <cudf/utilities/default_stream.hpp>

#include <nvbench/nvbench.cuh>

constexpr size_t data_size         = 256 << 20;
constexpr cudf::size_type num_cols = 64;

template <typename DataType>
void csv_read_common(DataType const& data_types,
                     cudf::io::io_type const& source_type,
                     nvbench::state& state)
{
  auto const tbl =
    create_random_table(cycle_dtypes(data_types, num_cols), table_size_bytes{data_size});
  auto const view = tbl->view();

  cuio_source_sink_pair source_sink(source_type);
  cudf::io::csv_writer_options options =
    cudf::io::csv_writer_options::builder(source_sink.make_sink_info(), view).include_header(true);

  cudf::io::write_csv(options);

  cudf::io::csv_reader_options const read_options =
    cudf::io::csv_reader_options::builder(source_sink.make_source_info());

  auto const mem_stats_logger = cudf::memory_stats_logger();  // init stats logger
  state.set_cuda_stream(nvbench::make_cuda_stream_view(cudf::default_stream_value.value()));
  state.exec(nvbench::exec_tag::sync | nvbench::exec_tag::timer,
             [&](nvbench::launch& launch, auto& timer) {
               try_drop_l3_cache();  // Drop L3 cache for accurate measurement

               timer.start();
               cudf::io::read_csv(read_options);
               timer.stop();
             });

  auto const time = state.get_summary("nv/cold/time/gpu/mean").get_float64("value");
  state.add_element_count(static_cast<double>(data_size) / time, "bytes_per_second");
  state.add_buffer_size(
    mem_stats_logger.peak_memory_usage(), "peak_memory_usage", "peak_memory_usage");
  state.add_buffer_size(source_sink.size(), "encoded_file_size", "encoded_file_size");
}

template <data_type DataType>
void BM_csv_read_input(nvbench::state& state, nvbench::type_list<nvbench::enum_type<DataType>>)
{
  cudf::rmm_pool_raii rmm_pool;

  auto const d_type      = get_type_or_group(static_cast<int32_t>(DataType));
  auto const source_type = io_type::FILEPATH;

  csv_read_common(d_type, source_type, state);
}

template <cudf::io::io_type IO>
void BM_csv_read_io(nvbench::state& state, nvbench::type_list<nvbench::enum_type<IO>>)
{
  cudf::rmm_pool_raii rmm_pool;

  auto const d_type      = get_type_or_group({static_cast<int32_t>(data_type::INTEGRAL),
                                         static_cast<int32_t>(data_type::FLOAT),
                                         static_cast<int32_t>(data_type::DECIMAL),
                                         static_cast<int32_t>(data_type::TIMESTAMP),
                                         static_cast<int32_t>(data_type::DURATION),
                                         static_cast<int32_t>(data_type::STRING)});
  auto const source_type = IO;

  csv_read_common(d_type, source_type, state);
}

using d_type_list = nvbench::enum_type_list<data_type::INTEGRAL,
                                            data_type::FLOAT,
                                            data_type::DECIMAL,
                                            data_type::TIMESTAMP,
                                            data_type::DURATION,
                                            data_type::STRING>;

using io_list =
  nvbench::enum_type_list<cudf::io::io_type::FILEPATH, cudf::io::io_type::HOST_BUFFER>;

NVBENCH_BENCH_TYPES(BM_csv_read_input, NVBENCH_TYPE_AXES(d_type_list))
  .set_name("csv_read_data_type")
  .set_type_axes_names({"data_type"})
  .set_min_samples(4);

NVBENCH_BENCH_TYPES(BM_csv_read_io, NVBENCH_TYPE_AXES(io_list))
  .set_name("csv_read_io")
  .set_type_axes_names({"io"})
  .set_min_samples(4);
