#ifndef GBWTGRAPH_GFA_H
#define GBWTGRAPH_GFA_H

#include <memory>

#include <gbwt/dynamic_gbwt.h>

#include <gbwtgraph/gbwtgraph.h>

/*
  gfa.h: Tools for building GBWTGraph from GFA.
*/

namespace gbwtgraph
{

//------------------------------------------------------------------------------

// TODO: Add sanity checks.
struct GFAParsingParameters
{
  // GBWT construction parameters. `node_width` and `batch_size` are not validated
  // at the moment.
  gbwt::size_type node_width = gbwt::WORD_BITS;
  gbwt::size_type batch_size = gbwt::DynamicGBWT::INSERT_BATCH_SIZE;
  gbwt::size_type sample_interval = gbwt::DynamicGBWT::SAMPLE_INTERVAL;

  // Chop segments longer than this into multiple nodes. Use 0 to disable chopping.
  size_t max_node_length = MAX_NODE_LENGTH;

  // To avoid creating too many jobs, combine small consecutive components into jobs
  // of at most `num_nodes / approximate_num_jobs` nodes. Value 0 is interpreted as 1.
  constexpr static size_t APPROXIMATE_NUM_JOBS = 32;
  size_t approximate_num_jobs = APPROXIMATE_NUM_JOBS;

  // Try to run this may construction jobs in parallel. Value 0 is interpreted as 1.
  size_t parallel_jobs = 1;

  // Determine GBWT batch size automatically. If the length of the longest path is `N`
  // segments, batch size will be the maximum of the default (100 million) and
  // `gbwt::DynamicGBWT::MIN_SEQUENCES_PER_BATCH * (N + 1)` but no more than GFA file
  // size in bytes. This should ensure that each batch consists of at least 10 paths
  // and their reverse complements. With heavy chopping, path length in nodes may be
  // much larger than `N`, and hence it may be useful to set the batch size manually.
  bool automatic_batch_size = true;

  bool show_progress = false;

  /*
    path_name_regex is the regex used for parsing path names. Each submatch (part of the
    regex separated by parentheses) is a field. The fields are numbered according to
    preorder traversal from left to right, with 0 corresponding to the entire path name.

    path_name_fields[i] maps field i to a GBWT path name component. Possible values are:

      S  sample name
      C  contig name
      H  haplotype identifier
      F  fragment identifier

    The values are case-insensitive. Any other character indicates that the field should
    not be used. If the string is too short, subsequent fields are not used. Each
    component may occur only once in the string.
  */
  const static std::string DEFAULT_REGEX; // .*
  const static std::string DEFAULT_FIELDS; // s

  std::string path_name_regex = DEFAULT_REGEX;
  std::string path_name_fields = DEFAULT_FIELDS;
};

//------------------------------------------------------------------------------

struct GFAExtractionParameters
{
  // Use this many OpenMP threads for extracting paths and walks. Value 0 is interpreted
  // as 1.
  size_t num_threads = 1;
  size_t threads() const { return std::max(this->num_threads, size_t(1)); }

  bool show_progress = false;
};

//------------------------------------------------------------------------------

/*
  Build GBWT from GFA P-lines and/or W-lines with the following assumptions:

    1. Links and paths have no overlaps between segments.
    2. There are no containments.

  If the construction fails, the function throws `std::runtime_error`.

  Before GBWT construction, the graph is partitioned into weakly connected
  components. The components are ordered by node ids, and contiguous ranges of
  components are assigned to jobs of roughly equal size. A separate GBWT index
  is built for each job, and the partial indexes are merged using the fast
  algorithm. Multiple jobs can be run in parallel.

  The construction is done in several passes over a memory-mapped GFA file. The
  function returns the GBWT index and a sequence source for GBWTGraph construction.

  If the GFA file contains both P-lines and W-lines, both will be used. In that
  case, P-lines will be interpreted as reference paths with sample name
  `REFERENCE_PATH_SAMPLE_NAME` and the path name as contig name. If there are only
  P-lines, GBWT metadata will be parsed using the regular expression defined in
  the parameters.

  If there are segments longer than the maximum length specified in the parameters,
  such segments will be broken into nodes of that length. If segment identifiers are
  not positive integers, they will be translated into such identifiers. In both
  cases, the sequence source will contain a translation from segment names to
  ranges of node identifiers.
*/
std::pair<std::unique_ptr<gbwt::GBWT>, std::unique_ptr<SequenceSource>>
gfa_to_gbwt(const std::string& gfa_filename, const GFAParsingParameters& parameters = GFAParsingParameters());

/*
  Writes the graph as GFA into the output stream in a normalized form. The lines are
  ordered in the following way:

  1. S-lines ordered by node ids.

  2. L-lines in canonical order. When using a single threads, the edges (from, to)
  are ordered by tuples (id(from), is_reverse(from), id(to), is_reverse(to)).
  All overlaps are `*`.

  3. P-lines for paths corresponding to sample `REFERENCE_PATH_SAMPLE_NAME`. When
  using a single thread, the paths are ordered by GBWT path ids. All overlaps are
  `*`.

  4. W-lines for other paths. When using a single thread, the paths are ordered by
  GBWT path ids.

  If the GBWT does not contain path names, all GBWT paths will be written as P-lines.
*/
void gbwt_to_gfa(const GBWTGraph& graph, std::ostream& out, const GFAExtractionParameters& parameters = GFAExtractionParameters());

extern const std::string GFA_EXTENSION; // ".gfa"

//------------------------------------------------------------------------------

} // namespace gbwtgraph

#endif // GBWTGRAPH_GFA_H
