#ifndef GBWTGRAPH_GBZ_H
#define GBWTGRAPH_GBZ_H

#include "gbwtgraph.h"
#include "utils.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>


/*
  gbz.h: GBZ file format.
*/

namespace gbwtgraph
{


namespace bi = boost::interprocess;

//------------------------------------------------------------------------------

/*
  GBZ file format wrapper, as specified in SERIALIZATION.md. The wrapper owns the
  GBWT index and the GBWTGraph.

  Constructors, serialization, and loading throw `std::runtime_error` on failure.

  File format versions:

    1  The initial version.
*/

template <typename CharAllocatorType>
class GBZ
{
public:
  GBZ(bi::managed_shared_memory* shared_memory = nullptr);
  GBZ(const GBZ& source);
  GBZ(GBZ&& source);
  ~GBZ();

  // Build GBZ from the structures returned by `gfa_to_gbwt()`.
  // Resets the pointers to `nullptr`.
  GBZ(std::unique_ptr<gbwt::GBWT>& index, std::unique_ptr<SequenceSource>& source, bi::managed_shared_memory* shared_memory = nullptr);

  // Build GBZ from a GBWT index and a `HandleGraph`.
  // Resets the GBWT pointer to `nullptr`.
  GBZ(std::unique_ptr<gbwt::GBWT>& index, const HandleGraph& source, bi::managed_shared_memory* shared_memory = nullptr);

  // Build GBZ from a GBWT index and a sequence source.
  // Note that the GBZ will store a copy of the GBWT index.
  GBZ(const gbwt::GBWT& index, const SequenceSource& source, bi::managed_shared_memory* shared_memory = nullptr);

  // Build GBZ from a GBWT index and a `HandleGraph`.
  // Note that the GBZ will store a copy of the GBWT index.
  GBZ(const gbwt::GBWT& index, const HandleGraph& source, bi::managed_shared_memory* shared_memory = nullptr);

  void swap(GBZ& another);
  GBZ& operator=(const GBZ& source);
  GBZ& operator=(GBZ&& source);

//------------------------------------------------------------------------------

  struct Header
  {
    std::uint32_t tag, version;
    std::uint64_t flags;

    constexpr static std::uint32_t TAG = 0x205A4247; // "GBZ "
    constexpr static std::uint32_t VERSION = Version::GBZ_VERSION;

    constexpr static std::uint64_t FLAG_MASK = 0x0000;

    Header();

    // Throws `sdsl::simple_sds::InvalidData` if the header is invalid.
    void check() const;

    void set_version() { this->version = VERSION; }

    void set(std::uint64_t flag) { this->flags |= flag; }
    void unset(std::uint64_t flag) { this->flags &= ~flag; }
    bool get(std::uint64_t flag) const { return (this->flags & flag); }

    bool operator==(const Header& another) const;
    bool operator!=(const Header& another) const { return !(this->operator==(another)); }
  };

//------------------------------------------------------------------------------

  Header     header;
  gbwt::Tags tags;
  gbwt::GBWT index;
  GBWTGraph<CharAllocatorType>  graph;
  bi::managed_shared_memory* shared_memory;

  const static std::string EXTENSION; // ".gbz"

  // Serialize the the GBZ into the output stream in the simple-sds format.
  void simple_sds_serialize(std::ostream& out) const;

  // Serialize the given GBWT and GBWTGraph objects in the GBZ format.
  static void simple_sds_serialize(const gbwt::GBWT& index, const GBWTGraph<CharAllocatorType>& graph, std::ostream& out);

  // Deserialize or decompress the GBZ from the input stream.
  void simple_sds_load(std::istream& in);

  // Returns the size of the serialized structure in elements.
  size_t simple_sds_size() const;

  // Serialize the GBWT (simple-sds format) and the GBWTGraph to separate files.
  // Default graph format is libhandlegraph / SDSL.
  void serialize_to_files(const std::string& gbwt_name, const std::string& graph_name, bool simple_sds_graph = false) const;

  // Loads the GBWT (simple-sds format) and the GBWTGraph from separate files.
  // Graph format is libhandlegraph / SDSL; the simple-sds format cannot be read.
  void load_from_files(const std::string& gbwt_name, const std::string& graph_name);

private:
  void copy(const GBZ& source);
  void reset_tags();
  void add_source();
  void set_gbwt();
  void set_gbwt_address();
};

//------------------------------------------------------------------------------

} // namespace gbwtgraph


#endif // GBWTGRAPH_GBZ_H
