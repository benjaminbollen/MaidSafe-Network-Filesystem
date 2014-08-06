/*  Copyright 2014 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/test.h"
#include "maidsafe/passport/passport.h"
#include "maidsafe/routing/parameters.h"

#include "maidsafe/nfs/client/maid_node_nfs.h"

namespace maidsafe {

namespace nfs {

namespace test {

static const size_t kTestChunkSize = 1024 * 1024;

class MaidNodeNfsTest : public testing::Test {
 public:
  MaidNodeNfsTest() {}

 protected:
  void AddClient() {
    auto maid_and_signer(passport::CreateMaidAndSigner());
    try {
      clients_.emplace_back(nfs_client::MaidNodeNfs::MakeShared(maid_and_signer));
    }
    catch (const std::exception& e) {
      // LOG(kError) << "AddClient failed:" << e.what();
      std::cout << "AddClient failed:" << e.what() << std::endl;
    }
  }

  void CompareGetResult(const std::vector<ImmutableData>& chunks,
                        std::vector<boost::future<ImmutableData>>& get_futures) {
    for (size_t index(0); index < chunks.size(); ++index) {
      try {
        auto retrieved(get_futures[index].get());
        EXPECT_EQ(retrieved.data(), chunks[index].data());
      }
      catch (const std::exception& ex) {
        EXPECT_TRUE(false) << "Failed to retrieve chunk: " << DebugId(chunks[index].name())
                           << " because: " << boost::diagnostic_information(ex);
      }
    }
  }

  void GenerateChunks(const size_t iterations, const size_t chunk_size = kTestChunkSize) {
    chunks_.clear();
    for (auto index(iterations); index > 0; --index) {
      chunks_.emplace_back(NonEmptyString(RandomString(chunk_size)));
      std::string str_count(std::to_string(iterations - index));
      if (iterations - index < 10)
        str_count.insert(std::begin(str_count), ' ');
      LOG(kInfo) << "Generated chunk " << str_count << " with id : "
                 << DebugId(chunks_.back().name());
    }
  }

  void PutGetTest(const size_t num_of_clients, const size_t num_of_chunks) {
    GenerateChunks(num_of_chunks);
    for (auto index(num_of_clients); index > 0; --index) {
//       std::cout << "client " << num_of_clients - index << " added" << std::endl;
      AddClient();
    }

    std::vector<boost::future<void>> put_futures;
    for (const auto& chunk : chunks_)
      put_futures.emplace_back(clients_[RandomInt32() % num_of_clients]->Put(chunk));
    int index(0);
    for (auto& future : put_futures) {
      EXPECT_NO_THROW(future.get()) << "Store failure "
                                    << DebugId(NodeId(chunks_[index].name()->string()));
//       std::cout << "Chunk " << index << " stored as "
//                             << DebugId(NodeId(chunks_[index].name()->string())) << std::endl;
      ++index;
    }

    std::vector<boost::future<ImmutableData>> get_futures;
    for (const auto& chunk : chunks_)
      get_futures.emplace_back(
          clients_[RandomInt32() % num_of_clients]->Get<ImmutableData::Name>(
              chunk.name(), std::chrono::seconds(
                                num_of_chunks + num_of_clients +
                                    routing::Parameters::default_response_timeout.count() / 1000)));
    CompareGetResult(chunks_, get_futures);
  }

  void VertionTreeTest(const size_t max_versions,
                       const size_t max_branches,
                       const size_t num_of_versions,
                       const size_t chunk_size = kTestChunkSize) {
    ImmutableData chunk(NonEmptyString(RandomAlphaNumericString(1024)));
    GenerateChunks(num_of_versions, chunk_size);
    StructuredDataVersions::VersionName v_ori(0, chunks_.front().name());
    AddClient();
    auto create_version_future(clients_.back()->CreateVersionTree(chunk.name(), v_ori,
      static_cast<uint32_t>(max_versions), static_cast<uint32_t>(max_branches)));
    EXPECT_NO_THROW(create_version_future.get()) << "failure to create version";

    size_t total_branches(1);
    // branch -> (version_index, chunk_index)
    std::vector<std::vector<std::pair<size_t, size_t>>> version_tree(
        max_branches, std::vector<std::pair<size_t, size_t>>());
    version_tree[0].push_back(std::make_pair(0, 0));

    std::vector<nfs_client::MaidNodeNfs::PutVersionFuture> put_version_futures;
    for (size_t index(1); index < chunks_.size(); ++index) {
      size_t cur_branch(RandomInt32() % total_branches);
      size_t cur_version_index(version_tree[cur_branch].back().first);
      std::shared_ptr<StructuredDataVersions::VersionName> v_old;
      bool fork(RandomInt32() % 2 == 0);
      size_t new_version_index(cur_version_index);
      if (fork && total_branches < max_branches && cur_version_index > 0) {
        for (auto& entry : version_tree[cur_branch])
          version_tree[total_branches].push_back(entry);
        version_tree[total_branches].pop_back();
        v_old.reset(new StructuredDataVersions::VersionName(cur_version_index,
            chunks_[version_tree[total_branches].back().second].name()));
        cur_branch = total_branches;
        ++total_branches;
      } else {
        v_old.reset(new StructuredDataVersions::VersionName(cur_version_index,
            chunks_[version_tree[cur_branch].back().second].name()));
        ++new_version_index;
      }
      version_tree[cur_branch].push_back(std::make_pair(new_version_index, index));
      StructuredDataVersions::VersionName v_new(new_version_index, chunks_[index].name());
      std::cout << "operation " << index << " put new version " << DebugId(v_new.id)
                << " after old version " << DebugId(v_old->id) << std::endl;
      put_version_futures.emplace_back(clients_.back()->PutVersion(chunk.name(), *v_old, v_new));
      Sleep(std::chrono::milliseconds(100));
    }

    for (size_t cur_branch(0); cur_branch < max_branches; ++cur_branch) {
      std::cout << "branch " << cur_branch << " containing versions : " << std::endl;
      for (auto& entry : version_tree[cur_branch])
        std::cout << " ( " << entry.first << " , "
                  << DebugId(chunks_[entry.second].name()) << " ) ";
      std::cout << std::endl;
    }

    size_t index(0);
    for (auto& put_version_future : put_version_futures)
      EXPECT_NO_THROW(put_version_future.get()) << "failure to put version "
          << DebugId(chunks_[++index].name());
    size_t num_of_tip_versions(0);
    try {
      auto future(clients_.back()->GetVersions(chunk.name()));
      auto versions(future.get());
      for (auto& version : versions)
        std::cout << "tip version : " << DebugId(version.id) << std::endl;
      num_of_tip_versions = versions.size();
      EXPECT_LE(num_of_tip_versions, max_branches);
      EXPECT_GT(num_of_tip_versions, size_t(0));
    } catch (const maidsafe_error& error) {
      EXPECT_TRUE(false) << "Failed to retrieve version: " << boost::diagnostic_information(error);
    }

    for (size_t cur_branch(0); cur_branch < max_branches; ++cur_branch) {
      try {
        StructuredDataVersions::VersionName v_tip(version_tree[cur_branch].back().first,
            chunks_[version_tree[cur_branch].back().second].name());
        auto future(clients_.back()->GetBranch(chunk.name(), v_tip));
        auto versions(future.get());
        std::cout << "get " << versions.size() << " versions for branch " << cur_branch
                  << std::endl;
        EXPECT_LE(versions.size(), version_tree[cur_branch].size());
        --num_of_tip_versions;
        auto chunk_index_itr(version_tree[cur_branch].end());
        for (size_t index(0); index < versions.size(); ++index) {
          std::cout << "version : " << DebugId(versions[index].id) << std::endl;
          if (chunk_index_itr != version_tree[cur_branch].begin()) {
            --chunk_index_itr;
            EXPECT_EQ(versions[index].id, chunks_[chunk_index_itr->second].name());
          }
        }
      } catch (const maidsafe_error& error) {
        LOG(kInfo) << "Failed to retrieve branch: " << boost::diagnostic_information(error);
      }
    }
    EXPECT_EQ(num_of_tip_versions, 0);
  }

  std::vector<ImmutableData> chunks_;
  std::vector<std::shared_ptr<nfs_client::MaidNodeNfs>> clients_;
};

TEST_F(MaidNodeNfsTest, FUNC_Constructor) {
  auto maid_and_signer(passport::CreateMaidAndSigner());
  {
    auto nfs_new_account = nfs_client::MaidNodeNfs::MakeShared(maid_and_signer);
  }
  LOG(kInfo) << "joining existing account";
  auto nfs_existing_account = nfs_client::MaidNodeNfs::MakeShared(maid_and_signer.first);
}

TEST_F(MaidNodeNfsTest, FUNC_FailingGet) {
  ImmutableData data(NonEmptyString(RandomString(kTestChunkSize)));
  AddClient();
  auto future(clients_.back()->Get<ImmutableData::Name>(data.name()));
  EXPECT_THROW(future.get(), std::exception) << "must have failed";
}

TEST_F(MaidNodeNfsTest, FUNC_GetTimeout) {
  AddClient();
  ImmutableData data(NonEmptyString(RandomString(kTestChunkSize)));
  try {
    auto future(clients_.back()->Put(data));
    EXPECT_NO_THROW(future.get());
  }
  catch (...) {
    EXPECT_TRUE(false) << "Failed to put: " << DebugId(NodeId(data.name()->string()));
  }

  std::chrono::steady_clock::duration timeout(std::chrono::microseconds(1));
  auto future(clients_.back()->Get<ImmutableData::Name>(data.name(), timeout));

  try {
    auto retrieved(future.get());
    EXPECT_TRUE(false) << "Unexpected get success: " << DebugId(NodeId(data.name()->string()));
  }
  catch (const maidsafe_error& error) {
    EXPECT_EQ(make_error_code(RoutingErrors::timed_out), error.code());
  }
}

TEST_F(MaidNodeNfsTest, FUNC_GetCancelled) {
  AddClient();
  ImmutableData data(NonEmptyString(RandomString(kTestChunkSize)));
  try {
    auto future(clients_.back()->Put(data));
    EXPECT_NO_THROW(future.get());
  }
  catch (...) {
    EXPECT_TRUE(false) << "Failed to put: " << DebugId(NodeId(data.name()->string()));
  }

  std::chrono::steady_clock::duration timeout(std::chrono::minutes(10));
  auto future(clients_.back()->Get<ImmutableData::Name>(data.name(), timeout));
  EXPECT_NO_THROW(clients_.back()->Stop());

  try {
    auto retrieved(future.get());
    EXPECT_TRUE(false) << "Unexpected get success: " << DebugId(NodeId(data.name()->string()));
  }
  catch (const maidsafe_error& error) {
    EXPECT_EQ(make_error_code(RoutingErrors::timer_cancelled), error.code());
  }
}

TEST_F(MaidNodeNfsTest, FUNC_PutGet) {
  AddClient();
  ImmutableData data(NonEmptyString(RandomString(kTestChunkSize)));
  LOG(kVerbose) << "Before put";
  try {
    auto future(clients_.back()->Put(data));
    EXPECT_NO_THROW(future.get());
    LOG(kVerbose) << "After put";
  } catch (...) {
    EXPECT_TRUE(false) << "Failed to put: " << DebugId(NodeId(data.name()->string()));
  }

  auto future(clients_.back()->Get<ImmutableData::Name>(data.name()));
  try {
    auto retrieved(future.get());
    EXPECT_EQ(retrieved.data(), data.data());
  } catch (...) {
    EXPECT_TRUE(false) << "Failed to retrieve: " << DebugId(NodeId(data.name()->string()));
  }
}

TEST_F(MaidNodeNfsTest, FUNC_MultipleSequentialPuts) {
  routing::Parameters::caching = true;
  const size_t kIterations(10);
  GenerateChunks(kIterations);
  AddClient();
  int index(0);
  for (const auto& chunk : chunks_) {
    auto future(clients_.back()->Put(chunk));
    EXPECT_NO_THROW(future.get()) << "Store failure " << DebugId(NodeId(chunk.name()->string()));
    LOG(kVerbose) << DebugId(NodeId(chunk.name()->string())) << " stored: " << index++;
  }

  std::vector<boost::future<ImmutableData>> get_futures;
  for (const auto& chunk : chunks_) {
    get_futures.emplace_back(clients_.back()->Get<ImmutableData::Name>(
        chunk.name(), std::chrono::seconds(kIterations * 10)));
  }
  CompareGetResult(chunks_, get_futures);
  LOG(kVerbose) << "Multiple sequential puts is finished successfully";
}

TEST_F(MaidNodeNfsTest, FUNC_MultipleParallelPuts) {
  routing::Parameters::caching = false;
  LOG(kVerbose) << "Put 20 chunks with 1 client";
  PutGetTest(1, 20);
  LOG(kVerbose) << "Multiple parallel puts test has finished successfully";
}

TEST_F(MaidNodeNfsTest, FUNC_MultipleClientsPut) {
  LOG(kVerbose) << "Put 10 chunks with 5 clients";
  PutGetTest(5, 10);
  LOG(kVerbose) << "Put with multiple clients test has finished successfully";
}

TEST_F(MaidNodeNfsTest, FUNC_DataFlooding) {
  LOG(kVerbose) << "Flooding 100 chunks with 10 clients";
  PutGetTest(10, 100);
  LOG(kVerbose) << "Data flooding test has finished successfully";
}

/*
// The test below is disbaled as its proper operation assumes a delete funcion is in place
TEST_F(MaidNodeNfsTest, DISABLED_FUNC_PutMultipleCopies) {
  ImmutableData data(NonEmptyString(RandomString(kTestChunkSize)));
  boost::future<ImmutableData> future;
  GetClients().front()->Put(data);
  Sleep(std::chrono::seconds(2));

  GetClients().back()->Put(data);
  Sleep(std::chrono::seconds(2));

  {
    future = GetClients().front()->Get<ImmutableData::Name>(data.name());
    try {
      auto retrieved(future.get());
      EXPECT_EQ(retrieved.data(), data.data());
    }
    catch (...) {
      EXPECT_TRUE(false) << "Failed to retrieve: " << DebugId(NodeId(data.name()->string()));
    }
  }

  LOG(kVerbose) << "1st successful put";

  {
    future = GetClients().back()->Get<ImmutableData::Name>(data.name());
    try {
      auto retrieved(future.get());
      EXPECT_EQ(retrieved.data(), data.data());
    }
    catch (...) {
      EXPECT_TRUE(false) << "Failed to retrieve: " << DebugId(NodeId(data.name()->string()));
    }
  }

  LOG(kVerbose) << "2nd successful put";

  GetClients().front()->Delete<ImmutableData::Name>(data.name());
  Sleep(std::chrono::seconds(2));

  LOG(kVerbose) << "1st Delete the chunk";

  try {
    auto retrieved(env_->Get<ImmutableData>(data.name()));
    EXPECT_EQ(retrieved.data(), data.data());
  }
  catch (...) {
    EXPECT_TRUE(false) << "Failed to retrieve: " << DebugId(NodeId(data.name()->string()));
  }

  LOG(kVerbose) << "Chunk still exist as expected";

  GetClients().back()->Delete<ImmutableData::Name>(data.name());
  Sleep(std::chrono::seconds(2));

  LOG(kVerbose) << "2nd Delete the chunk";

  routing::Parameters::caching = false;

  EXPECT_THROW(env_->Get<ImmutableData>(data.name()), std::exception)
      << "Should have failed to retreive";

  LOG(kVerbose) << "PutMultipleCopies Succeeds";
  routing::Parameters::caching = true;
}
*/


TEST_F(MaidNodeNfsTest, FUNC_PopulateSingleBranchTree) {
  ImmutableData chunk(NonEmptyString(RandomAlphaNumericString(1024)));
  const size_t max_versions(5), max_branches(1);
  GenerateChunks(max_versions * 2);
  StructuredDataVersions::VersionName v_ori(0, chunks_.front().name());
  AddClient();
  auto create_version_future(clients_.back()->CreateVersionTree(chunk.name(), v_ori,
      static_cast<uint32_t>(max_versions), static_cast<uint32_t>(max_branches)));
  EXPECT_NO_THROW(create_version_future.get()) << "failure to create version";
  for (size_t index(1); index < (max_versions * 2); ++index) {
    StructuredDataVersions::VersionName v_old(index - 1, chunks_[index - 1].name());
    StructuredDataVersions::VersionName v_new(index, chunks_[index].name());
    auto put_version_future(clients_.back()->PutVersion(chunk.name(), v_old, v_new));
    EXPECT_NO_THROW(put_version_future.get()) << "failure to put version " << index;
//     std::cout << "version " << index << " has been put" << std::endl;
  }
  try {
    auto future(clients_.back()->GetVersions(chunk.name()));
    auto versions(future.get());
    EXPECT_EQ(versions.size(), max_branches);
//     for (auto& version : versions)
//       std::cout << "tip version : " << DebugId(version.id) << std::endl;
    EXPECT_EQ(versions.front().index, max_versions * 2 - 1);
    EXPECT_EQ(versions.front().id, chunks_.back().name());
  } catch (const maidsafe_error& error) {
    EXPECT_TRUE(false) << "Failed to retrieve version: " << boost::diagnostic_information(error);
  }

  try {
    StructuredDataVersions::VersionName v_tip(max_versions * 2 - 1, chunks_.back().name());
    auto future(clients_.back()->GetBranch(chunk.name(), v_tip));
    auto versions(future.get());
    EXPECT_EQ(versions.size(), max_versions);
    for (size_t index(0); index < versions.size(); ++index) {
//       std::cout << "version : " << DebugId(versions[index].id) << std::endl;
      EXPECT_EQ(versions[index].index, max_versions * 2 - index - 1);
      EXPECT_EQ(versions[index].id, chunks_[max_versions * 2 - index - 1].name());
    }
  } catch (const maidsafe_error& error) {
    EXPECT_TRUE(false) << "Failed to retrieve branch: " << boost::diagnostic_information(error);
  }
}

TEST_F(MaidNodeNfsTest, FUNC_PopulateMultipleBranchTree) {
  LOG(kInfo) << "Testing with max_versions = 5, max_branches = 4, total_versions = 20";
  VertionTreeTest(5, 4, 20);
  LOG(kInfo) << "Testing with max_versions = 100, max_branches = 10, total_versions = 60";
  VertionTreeTest(100, 10, 60);
}

TEST_F(MaidNodeNfsTest, FUNC_PopulateLengthyTree) {
  LOG(kInfo) << "Testing with max_versions = 100, max_branches = 1, total_versions = 1500";
  VertionTreeTest(100, 1, 1500, 256);
  LOG(kInfo) << "Testing with max_versions = 15000, max_branches = 1, total_versions = 14580";
  VertionTreeTest(15000, 1, 14580, 128);
  LOG(kInfo) << "Testing with max_versions = 100, max_branches = 1, total_versions = 20000";
  VertionTreeTest(100, 1, 20000, 128);
}

}  // namespace test

}  // namespace nfs

}  // namespace maidsafe
