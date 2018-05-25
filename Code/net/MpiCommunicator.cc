//
// Copyright (C) University College London, 2007-2012, all rights reserved.
//
// This file is part of HemeLB and is CONFIDENTIAL. You may not work
// with, install, use, duplicate, modify, redistribute or share this
// file, or any part thereof, other than as allowed by any agreement
// specifically made by you with University College London.
//

#include <cassert>
#include <numeric>

#include "net/MpiCommunicator.h"
#include "net/MpiGroup.h"
#include "util/Iterator.h"

namespace hemelb
{
  namespace net
  {
    namespace
    {
      void Deleter(MPI_Comm* comm)
      {
        int finalized;
        HEMELB_MPI_CALL(MPI_Finalized, (&finalized));
        if (!finalized)
          HEMELB_MPI_CALL(MPI_Comm_free, (comm));
        delete comm;
      }

      int GetMPICommRank(MPI_Comm comm)
      {
        int rank;
        HEMELB_MPI_CALL(MPI_Comm_rank, (comm, &rank));
        return rank;
      }

      int GetMPICommSize(MPI_Comm comm)
      {
        int size;
        HEMELB_MPI_CALL(MPI_Comm_size, (comm, &size));
        return size;
      }
    }

    MpiCommunicator MpiCommunicator::World()
    {
      return MpiCommunicator(MPI_COMM_WORLD, false);
    }

    MpiCommunicator::MpiCommunicator() :
        commPtr(), communicatorSize(-1), localRankInCommunicator(-1)
    {
    }

    MpiCommunicator::MpiCommunicator(MpiCommunicator const & comm) :
        commPtr(comm.commPtr), communicatorSize(comm.communicatorSize), localRankInCommunicator(comm.localRankInCommunicator)
    {
    }

    MpiCommunicator::MpiCommunicator(MpiCommunicator && comm) :
        commPtr(std::move(comm.commPtr)), communicatorSize(comm.communicatorSize), localRankInCommunicator(comm.localRankInCommunicator)
    {
    }

    MpiCommunicator::MpiCommunicator(MPI_Comm communicator, bool owner) :
        commPtr(), communicatorSize(-1), localRankInCommunicator(-1)
    {
      if (communicator == MPI_COMM_NULL)
        return;

      if (owner)
      {
        commPtr.reset(new MPI_Comm(communicator), Deleter);
      }
      else
      {
        commPtr.reset(new MPI_Comm(communicator));
      }

      communicatorSize = GetMPICommSize(communicator);
      localRankInCommunicator = GetMPICommRank(communicator);
    }

    MpiCommunicator::~MpiCommunicator()
    {
    }

    void MpiCommunicator::operator =(MpiCommunicator const &comm)
    {
      commPtr = comm.commPtr;
      communicatorSize = comm.communicatorSize;
      localRankInCommunicator = comm.localRankInCommunicator;
    }

    void MpiCommunicator::operator =(MpiCommunicator &&comm)
    {
      commPtr = std::move(comm.commPtr);
      communicatorSize = comm.communicatorSize;
      localRankInCommunicator = comm.localRankInCommunicator;
    }

    bool operator==(const MpiCommunicator& comm1, const MpiCommunicator& comm2)
    {
      if (comm1)
      {
        if (comm2)
        {
          int result;
          HEMELB_MPI_CALL(MPI_Comm_compare, (comm1, comm2, &result));
          return result == MPI_IDENT;
        }
        return false;
      }
      return (!comm2);
    }

    bool operator!=(const MpiCommunicator& comm1, const MpiCommunicator& comm2)
    {
      return ! (comm1 == comm2);
    }

    inline int MpiCommunicator::Rank() const
    {
      assert(localRankInCommunicator != -1);
      return localRankInCommunicator;
    }

    void MpiCommunicator::Barrier() const
    {
      HEMELB_MPI_CALL(MPI_Barrier, (*commPtr));
    }

    inline int MpiCommunicator::Size() const
    {
      assert(communicatorSize != -1);
      return communicatorSize;
    }

    MpiGroup MpiCommunicator::Group() const
    {
      MPI_Group grp;
      HEMELB_MPI_CALL(MPI_Comm_group, (*commPtr, &grp));
      return MpiGroup(grp, true);
    }

    MpiCommunicator MpiCommunicator::Create(const MpiGroup& grp) const
    {
      MPI_Comm newComm;
      HEMELB_MPI_CALL(MPI_Comm_create, (*commPtr, grp, &newComm));
      return MpiCommunicator(newComm, true);
    }

    void MpiCommunicator::Abort(int errCode) const
    {
      HEMELB_MPI_CALL(MPI_Abort, (*commPtr, errCode));
    }

    MpiCommunicator MpiCommunicator::Duplicate() const
    {
      MPI_Comm newComm;
      HEMELB_MPI_CALL(MPI_Comm_dup, (*commPtr, &newComm));
      return MpiCommunicator(newComm, true);
    }

    MpiCommunicator MpiCommunicator::Graph(std::vector<std::vector<int>> edges, bool reorder) const
    {
      std::vector<int> indices, flat_edges;
      indices.reserve(1);
      flat_edges.reserve(1);
      for (auto const & edge_per_proc : edges)
      {
        for (auto const & edge : edge_per_proc)
        {
          assert(edge < Size());
          flat_edges.push_back(edge);
        }
        indices.push_back(flat_edges.size());
      }
      MPI_Comm newComm;
      HEMELB_MPI_CALL(MPI_Graph_create,
                      (*commPtr, indices.size(), indices.data(), flat_edges.data(), reorder, &newComm));
      return MpiCommunicator(newComm, true);
    }

    int MpiCommunicator::GetNeighborsCount() const
    {
      return GetNeighborsCount(Rank());
    }

    std::vector<int> MpiCommunicator::GetNeighbors() const
    {
      return GetNeighbors(Rank());
    }

    int MpiCommunicator::GetNeighborsCount(int whoseNeighbours) const
    {
      assert(commPtr);
      int N;
      HEMELB_MPI_CALL(MPI_Graph_neighbors_count, (*commPtr, whoseNeighbours, &N));
      return N;
    }

    std::vector<int> MpiCommunicator::GetNeighbors(int whoseNeighbours) const
    {
      assert(commPtr);
      std::vector<int> result(GetNeighborsCount(whoseNeighbours));
      result.reserve(1);
      HEMELB_MPI_CALL(MPI_Graph_neighbors, (*commPtr, whoseNeighbours, result.size(), result.data()));
      return result;
    }

    MpiCommunicator MpiCommunicator::Split(int color, int key) const
    {
      MPI_Comm newComm;
      MPI_Comm_split(*commPtr, color, key, &newComm);
      return MpiCommunicator(newComm, true);
    }

    std::map<int, int> MpiCommunicator::RankMap(MpiCommunicator const &valueComm) const
    {
      MPI_Group keyGroup, valueGroup;

      MPI_Comm_group(*commPtr, &keyGroup);
      MPI_Comm_group(valueComm, &valueGroup);

      auto const N = Size();
      std::vector<int> keys(N), values(N);
      std::iota(keys.begin(), keys.end(), 0);

      MPI_Group_translate_ranks(keyGroup, N, keys.data(), valueGroup, values.data());
      std::map<int, int> result;
      for (auto const & item : util::zip(keys, values))
      {
        result[std::get<0>(item)] = std::get<1>(item);
      }

      MPI_Group_free(&keyGroup);
      MPI_Group_free(&valueGroup);

      return result;
    }

  }
}
