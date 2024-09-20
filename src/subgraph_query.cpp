#include "absl/log/absl_log.h"
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <getopt.h>

#include <gbwtgraph/gbz.h>
#include <gbwtgraph/subgraph.h>

using namespace gbwtgraph;

//------------------------------------------------------------------------------

const std::string tool_name = "Subgraph Query";

struct Config
{
  Config(int argc, char** argv);

  std::string graph_file;

  SubgraphQuery::QueryType query_type = SubgraphQuery::QueryType::invalid_query;
  SubgraphQuery::HaplotypeOutput haplotype_output = SubgraphQuery::HaplotypeOutput::all_haplotypes;

  std::string sample_name = REFERENCE_PATH_SAMPLE_NAME, contig_name = "";

  size_t offset = 0, limit = 0;
  nid_t node_id = 0;
  size_t context = 100;
};

std::unique_ptr<PathIndex> create_path_index(const GBZ& gbz, const SubgraphQuery& query)
{
  if(query.type == SubgraphQuery::QueryType::path_offset_query || query.type == SubgraphQuery::QueryType::path_interval_query)
  {
    return std::make_unique<PathIndex>(gbz);
  }
  return nullptr;
}

path_handle_t find_reference_path(const GBZ& gbz, const Config& config)
{
  const gbwt::Metadata& metadata = gbz.index.metadata;
  std::vector<gbwt::size_type> path_ids = metadata.findPaths(metadata.sample(config.sample_name), metadata.contig(config.contig_name));
  if(path_ids.size() != 1)
  {
    std::string msg = "Found " + std::to_string(path_ids.size()) + " reference paths for sample " + config.sample_name + ", contig " + config.contig_name;
    ABSL_LOG(FATAL) << msg;
  }
  return gbz.graph.path_to_handle(path_ids.front());
}

SubgraphQuery create_query(const GBZ& gbz, const Config& config)
{
  switch(config.query_type)
  {
  case SubgraphQuery::QueryType::path_offset_query:
    {
      path_handle_t path = find_reference_path(gbz, config);
      return SubgraphQuery::path_offset(path, config.offset, config.context, config.haplotype_output);
    }
  case SubgraphQuery::QueryType::path_interval_query:
    {
      path_handle_t path = find_reference_path(gbz, config);
      return SubgraphQuery::path_interval(path, config.offset, config.limit, config.context, config.haplotype_output);
    }
  case SubgraphQuery::QueryType::node_query:
    return SubgraphQuery::node(config.node_id, config.context, config.haplotype_output);
  default:
    ABSL_LOG(FATAL) << "Unknown query type";
  }
}

//------------------------------------------------------------------------------

int
main(int argc, char** argv)
{
  double start = gbwt::readTimer();

  try
  {
    Config config(argc, argv);

    GBZ gbz;
    sdsl::simple_sds::load_from(gbz, config.graph_file);
    SubgraphQuery query = create_query(gbz, config);
    std::unique_ptr<PathIndex> path_index = create_path_index(gbz, query);

    Subgraph subgraph(gbz, path_index.get(), query);
    subgraph.to_gfa(gbz, std::cout);
    std::cout.flush();
  }
  catch(const std::runtime_error& e)
  {
    std::cerr << "subgraph_query: " << "" << std::endl;
  }

  double seconds = gbwt::readTimer() - start;
  std::cerr << "Used " << seconds << " seconds, " << gbwt::inGigabytes(gbwt::memoryUsage()) << " GiB" << std::endl;

  return 0;
}

//------------------------------------------------------------------------------

void
printUsage(int exit_code)
{
  Version::print(std::cerr, tool_name);

  std::cerr << "Usage: subgraph_query [options] graph.gbz" << std::endl;
  std::cerr << std::endl;
  std::cerr << "Options:" << std::endl;
  std::cerr << "  --sample NAME     sample name for the reference path (default: no sample name)" << std::endl;
  std::cerr << "  --contig NAME     contig name for the reference path (required for --offset and --interval)" << std::endl;
  std::cerr << "  --offset N        query a reference path at offset N" << std::endl;
  std::cerr << "  --interval M..N   query a reference path in interval [M, N)" << std::endl;
  std::cerr << "  --node N          query a node with id N" << std::endl;
  std::cerr << "  --context N       context length around the query position in bp (default: 100)" << std::endl;
  std::cerr << "  --distinct        output distinct haplotypes only" << std::endl;
  std::cerr << "  --reference-only  only output the reference path" << std::endl;
  std::cerr << std::endl;

  std::exit(exit_code);
}

//------------------------------------------------------------------------------

Config::Config(int argc, char** argv)
{
  if(argc < 2) { printUsage(EXIT_SUCCESS); }

  constexpr int OPT_SAMPLE = 1000;
  constexpr int OPT_CONTIG = 1001;
  constexpr int OPT_OFFSET = 1002;
  constexpr int OPT_INTERVAL = 1003;
  constexpr int OPT_NODE = 1004;
  constexpr int OPT_CONTEXT = 1005;
  constexpr int OPT_DISTINCT = 1006;
  constexpr int OPT_REFERENCE_ONLY = 1007;

  // Data for `getopt_long()`.
  int c = 0, option_index = 0;
  option long_options[] =
  {
    { "sample", required_argument, 0, OPT_SAMPLE },
    { "contig", required_argument, 0, OPT_CONTIG },
    { "offset", required_argument, 0, OPT_OFFSET },
    { "interval", required_argument, 0, OPT_INTERVAL },
    { "node", required_argument, 0, OPT_NODE },
    { "context", required_argument, 0, OPT_CONTEXT },
    { "distinct", no_argument, 0, OPT_DISTINCT },
    { "reference-only", no_argument, 0, OPT_REFERENCE_ONLY },
    { 0, 0, 0, 0 }
  };

  // Process options.
  while((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1)
  {
    switch(c)
    {
    case OPT_SAMPLE:
      this->sample_name = optarg;
      break;
    case OPT_CONTIG:
      this->contig_name = optarg;
      break;
    case OPT_OFFSET:
      this->query_type = SubgraphQuery::QueryType::path_offset_query;
      this->offset = std::stoul(optarg);
      break;
    case OPT_INTERVAL:
      this->query_type = SubgraphQuery::QueryType::path_interval_query;
      {
        std::string interval(optarg);
        size_t pos = interval.find("..");
        if(pos == std::string::npos)
        {
          std::string msg = "Invalid path interval: " + interval;
          ABSL_LOG(FATAL) << msg;
        }
        this->offset = std::stoul(interval.substr(0, pos));
        this->limit = std::stoul(interval.substr(pos + 2));
      }
      break;
    case OPT_NODE:
      this->query_type = SubgraphQuery::QueryType::node_query;
      this->node_id = std::stoul(optarg);
      break;
    case OPT_CONTEXT:
      this->context = std::stoul(optarg);
      break;
    case OPT_DISTINCT:
      this->haplotype_output = SubgraphQuery::HaplotypeOutput::distinct_haplotypes;
      break;
    case OPT_REFERENCE_ONLY:
      this->haplotype_output = SubgraphQuery::HaplotypeOutput::reference_only;
      break;

    case '?':
      std::exit(EXIT_FAILURE);
    default:
      std::exit(EXIT_FAILURE);
    }
  }

  // Sanity checks.
  if(optind >= argc)
  {
    std::string msg = "Missing graph file";
    ABSL_LOG(FATAL) << msg;
  }
  this->graph_file = argv[optind]; optind++;
  if(this->query_type == SubgraphQuery::QueryType::invalid_query)
  {
    std::string msg = "Path offset or interval or node id is required";
    ABSL_LOG(FATAL) << msg;
  }
  if((this->query_type == SubgraphQuery::QueryType::path_offset_query || this->query_type == SubgraphQuery::QueryType::path_interval_query) && this->contig_name.empty())
  {
    std::string msg = "Contig name is required for path offset or interval";
    ABSL_LOG(FATAL) << msg;
  }
}

//------------------------------------------------------------------------------
