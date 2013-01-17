/***************************************************************************************************
 *  Copyright 2012 MaidSafe.net limited                                                            *
 *                                                                                                 *
 *  The following source code is property of MaidSafe.net limited and is not meant for external    *
 *  use.  The use of this code is governed by the licence file licence.txt found in the root of    *
 *  this directory and also on www.maidsafe.net.                                                   *
 *                                                                                                 *
 *  You are not free to copy, amend or otherwise use this source code without the explicit         *
 *  written permission of the board of directors of MaidSafe.net.                                  *
 **************************************************************************************************/

#include "maidsafe/nfs/utils.h"

#include <cstdint>

#include "maidsafe/common/utils.h"


namespace maidsafe {

namespace nfs {

namespace detail {

MessageId GetNewMessageId(const NodeId& source_node_id) {
  static int32_t random_element(RandomInt32());
  std::string id(source_node_id.string() + std::to_string(random_element++));
  return MessageId(Identity(crypto::Hash<crypto::SHA512>(source_node_id.string() +
                                                         std::to_string(random_element++))));
}

}  // namespace detail


void HandlePostResponse(GenericMessage::OnError /*on_error_functor*/,
                        GenericMessage /*original_generic_message*/,
                        const std::vector<std::string>& /*serialised_messages*/) {
  // TODO(Team): BEFORE_RELEASE implement
}

}  // namespace nfs

}  // namespace maidsafe
