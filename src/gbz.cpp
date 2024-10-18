#include "absl/log/absl_log.h"
#include <boost/interprocess/sync/named_mutex.hpp>
#include <gbwt/gbwt.h>
#include <gbwt/utils.h>
#include <gbwtgraph/gbwtgraph.h>
#include <gbwtgraph/utils.h>
#include <sdsl/simple_sds.hpp>
#include <gbwtgraph/gbz.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace gbwtgraph
{


namespace bi = boost::interprocess;


//------------------------------------------------------------------------------

// Numerical class constants.
template <typename CharAllocatorType>
constexpr std::uint32_t GBZ<CharAllocatorType>::Header::TAG;

template <typename CharAllocatorType>
constexpr std::uint32_t GBZ<CharAllocatorType>::Header::VERSION;

template <typename CharAllocatorType>
constexpr std::uint64_t GBZ<CharAllocatorType>::Header::FLAG_MASK;

//------------------------------------------------------------------------------

// Other class variables.

template <typename CharAllocatorType>
const std::string GBZ<CharAllocatorType>::EXTENSION = ".gbz";

//------------------------------------------------------------------------------

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::Header::Header() :
  tag(TAG), version(VERSION),
  flags(0)
{
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::Header::check() const
{
  if(this->tag != TAG)
  {
    ABSL_LOG(FATAL) << "GBZ: Invalid tag";
  }

  if(this->version != VERSION)
  {
    std::string msg = "GBZ: Expected v" + std::to_string(VERSION) + ", got v" + std::to_string(this->version);
    ABSL_LOG(FATAL) << msg;
  }

  std::uint64_t mask = 0;
  switch(this->version)
  {
  case VERSION:
    mask = FLAG_MASK; break;
  }
  if((this->flags & mask) != this->flags)
  {
    ABSL_LOG(FATAL) << "GBZ: Invalid flags";
  }
}

template <typename CharAllocatorType>
bool
GBZ<CharAllocatorType>::Header::operator==(const Header& another) const
{
  return (this->tag == another.tag && this->version == another.version &&
          this->flags == another.flags);
}

//------------------------------------------------------------------------------

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::GBZ(bi::managed_shared_memory* shared_memory)
{
  this->graph.set_shared_memory(shared_memory);
  this->add_source();
  this->set_gbwt();
  this->shared_memory = shared_memory;
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::GBZ(const GBZ& source)
{
  this->copy(source);
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::GBZ(GBZ&& source)
{
  *this = std::move(source);
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::~GBZ()
{}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::swap(GBZ& another)
{
  if(&another == this) { return; }

  std::swap(this->header, another.header);
  this->tags.swap(another.tags);
  this->index.swap(another.index);
  this->graph.swap(another.graph);

  // GBWTGraph did not know that we also swapped the GBWTs.
  this->set_gbwt_address();
  another.set_gbwt_address();
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>&
GBZ<CharAllocatorType>::operator=(const GBZ& source)
{
  if(&source != this) { this->copy(source); }
  return *this;
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>&
GBZ<CharAllocatorType>::operator=(GBZ&& source)
{
  if(&source != this)
  {
    this->header = std::move(source.header);
    this->tags = std::move(source.tags);
    this->index = std::move(source.index);
    this->graph = std::move(source.graph);

    // GBWTGraph did not know that we also moved the GBWT.
    this->set_gbwt_address();
  }
  return *this;
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::copy(const GBZ& source)
{
  this->header = source.header;
  this->tags = source.tags;
  this->index = source.index;
  this->graph = source.graph;

  // Use the local copy of the GBWT.
  this->set_gbwt_address();
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::reset_tags()
{
  this->tags.clear();
  this->add_source();
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::add_source()
{
  this->tags.set(Version::SOURCE_KEY, Version::SOURCE_VALUE);
}

//------------------------------------------------------------------------------

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::GBZ(std::unique_ptr<gbwt::GBWT>& index, std::unique_ptr<SequenceSource>& source, bi::managed_shared_memory* shared_memory)
{
  if(index == nullptr || source == nullptr)
  {
    ABSL_LOG(FATAL) << "GBZ: Index and sequence source must be non-null";
  }

  this->add_source();
  this->index = std::move(*index); index.reset();
  this->graph = GBWTGraph<CharAllocatorType>(this->index, *source, shared_memory); source.reset();
  this->shared_memory = shared_memory;
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::GBZ(std::unique_ptr<gbwt::GBWT>& index, const HandleGraph& source, bi::managed_shared_memory* shared_memory)
{
  if(index == nullptr)
  {
    ABSL_LOG(FATAL) << "GBZ: Index must be non-null";
  }

  this->add_source();
  this->index = std::move(*index); index.reset();
  this->graph = GBWTGraph<CharAllocatorType>(this->index, source, nullptr, shared_memory);
  this->shared_memory = shared_memory;
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::GBZ(const gbwt::GBWT& index, const SequenceSource& source, bi::managed_shared_memory* shared_memory) :
  index(index)
{
  this->add_source();
  this->graph = GBWTGraph<CharAllocatorType>(this->index, source, shared_memory);
  this->shared_memory = shared_memory;
}

template <typename CharAllocatorType>
GBZ<CharAllocatorType>::GBZ(const gbwt::GBWT& index, const HandleGraph& source, bi::managed_shared_memory* shared_memory) :
  index(index)
{
  this->add_source();
  this->graph = GBWTGraph<CharAllocatorType>(this->index, source, nullptr, shared_memory);
  this->shared_memory = shared_memory;
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::set_gbwt()
{
  this->graph.set_gbwt(this->index);
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::set_gbwt_address()
{
  this->graph.set_gbwt_address(this->index);
}

//------------------------------------------------------------------------------

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::simple_sds_serialize(std::ostream& out) const
{
  sdsl::simple_sds::serialize_value(this->header, out);
  this->tags.simple_sds_serialize(out);
  this->index.simple_sds_serialize(out);
  this->graph.simple_sds_serialize(out);
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::simple_sds_serialize(const gbwt::GBWT& index, const GBWTGraph<CharAllocatorType>& graph, std::ostream& out)
{
  GBZ<std::allocator<char>> empty;
  sdsl::simple_sds::serialize_value(empty.header, out);
  empty.tags.simple_sds_serialize(out);
  index.simple_sds_serialize(out);
  graph.simple_sds_serialize(out);
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::simple_sds_load(std::istream& in)
{
  this->header = sdsl::simple_sds::load_value<Header>(in);
  this->header.check();

  // Load the tags and update the source to this library.
  // We could also check if the source was already this library, but we have no
  // uses for that information at the moment.
  this->tags.simple_sds_load(in);
  this->add_source();

  this->index.simple_sds_load(in);
  this->graph.simple_sds_load(in, this->index);
}

template <typename CharAllocatorType>
size_t
GBZ<CharAllocatorType>::simple_sds_size() const
{
  size_t result = sdsl::simple_sds::value_size(this->header);
  result += this->tags.simple_sds_size();
  result += this->index.simple_sds_size();
  result += this->graph.simple_sds_size();
  return result;
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::serialize_to_files(const std::string& gbwt_name, const std::string& graph_name, bool simple_sds_graph) const
{
  sdsl::simple_sds::serialize_to(this->index, gbwt_name);
  if(simple_sds_graph) { sdsl::simple_sds::serialize_to(this->graph, graph_name); }
  else { this->graph.serialize(graph_name); }
}

template <typename CharAllocatorType>
void
GBZ<CharAllocatorType>::load_from_files(const std::string& gbwt_name, const std::string& graph_name)
{
  this->tags.clear();
  this->add_source();
  sdsl::simple_sds::load_from(this->index, gbwt_name);
  this->set_gbwt();
  this->graph.deserialize(graph_name);
}

template class GBZ<std::allocator<char>>;
template class GBZ<gbwt::SharedMemCharAllocatorType>;

//------------------------------------------------------------------------------

} // namespace gbwtgraph
