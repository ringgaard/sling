// Copyright 2022 Ringgaard Research ApS
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/random.h>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/file/file.h"
#include "sling/frame/store.h"
#include "sling/frame/decoder.h"
#include "sling/frame/encoder.h"
#include "sling/frame/reader.h"
#include "sling/frame/wire.h"
#include "sling/db/dbclient.h"
#include "sling/net/http-server.h"
#include "sling/net/web-sockets.h"
#include "sling/stream/input.h"
#include "sling/stream/file.h"
#include "sling/stream/memory.h"
#include "sling/string/strcat.h"
#include "sling/util/mutex.h"
#include "sling/util/queue.h"
#include "sling/util/thread.h"
#include "sling/util/unicode.h"

using namespace sling;

DEFINE_string(addr, "", "HTTP server address");
DEFINE_int32(port, 7700, "HTTP server port");
DEFINE_int32(workers, 16, "Number of network worker threads");
DEFINE_int32(flush, 30, "Number of seconds before writing changes to disk");
DEFINE_int32(ping, 30, "Number of seconds between keep-alive pings");
DEFINE_int32(onetime_invite, false, "Invalidate invite when joining");
DEFINE_string(datadir, ".", "Data directory for collaborations");
DEFINE_string(pubdb, "", "Case publishing database");

// Collaboration protocol opcodes.
enum CollabOpcode {
  COLLAB_CREATE   = 1,    // create new collaboration
  COLLAB_DELETE   = 2,    // delete collaboration
  COLLAB_INVITE   = 3,    // invite participant to join collaboration
  COLLAB_JOIN     = 4,    // add user as participant in collaboration
  COLLAB_LOGIN    = 5,    // log-in to collaboration to send and receive updates
  COLLAB_NEWID    = 6,    // new topic id
  COLLAB_UPDATE   = 7,    // update collaboration case
  COLLAB_FLUSH    = 8,    // flush changes to disk
  COLLAB_IMPORT   = 9,    // bulk import topics
  COLLAB_SEARCH   = 10,   // search for matching topics
  COLLAB_TOPICS   = 12,   // retrieve tropics
  COLLAB_LABELS   = 13,   // retrieve labels for topics
  COLLAB_REDIRECT = 14,   // redirect all reference to topic to another
  COLLAB_SHARE    = 15,   // share/publish collaboration case

  COLLAB_ERROR   = 127,  // error message
};

// Collaboration case update types.
enum CollabUpdate {
  CCU_TOPIC   = 1,    // topic updated
  CCU_FOLDER  = 2,    // folder updated
  CCU_FOLDERS = 3,    // folder list updated
  CCU_DELETE  = 4,    // topic deleted
  CCU_RENAME  = 5,    // folder renamed
  CCU_SAVE    = 6,    // case saved
  CCU_TOPICS  = 7,    // topics updated
};

// Collaboration search flags.
enum CollabSearchFlags {
  CS_FULL      = 1,    // full match
  CS_KEYWORD   = 2,    // keyword match
};

// Credential key size.
const int CREDENTIAL_BITS = 128;
const int CREDENTIAL_BYTES = CREDENTIAL_BITS / 8;

class CollabCase;
class CollabClient;
class CollabService;

// Global mutex for serializing access to collaboration server.
Mutex mu;

// HTTP server.
HTTPServer *httpd = nullptr;

// Collaboration server.
CollabService *collabd = nullptr;

// Case publishing database.
DBClient pubdb;

// Commons store with global symbols.
Store *commons;
Names names;
Name n_caseid(names, "caseid");
Name n_main(names, "main");
Name n_topics(names, "topics");
Name n_folders(names, "folders");
Name n_modified(names, "modified");
Name n_shared(names, "shared");
Name n_share(names, "share");
Name n_publish(names, "publish");
Name n_lazyload(names, "lazyload");
Name n_next(names, "next");
Name n_author(names, "P50");
Name n_participant(names, "P710");
Name n_name(names, "name");
Name n_alias(names, "alias");
Name n_birth_name(names, "P1477");
Name n_married_name(names, "P2562");
Name n_description(names, "description");
Name n_ref(names, "ref");
Name n_topic(names, "topic");

// Return random key encoded as hex digits.
string RandomKey() {
  uint8 key[CREDENTIAL_BYTES];
  CHECK_EQ(getrandom(key, CREDENTIAL_BYTES, 0), CREDENTIAL_BYTES);
  string str;
  for (int i = 0; i < CREDENTIAL_BYTES; ++i) {
    str.push_back("0123456789abcdef"[key[i] >> 4]);
    str.push_back("0123456789abcdef"[key[i] & 0x0F]);
  }
  return str;
}

// Topic name index.
class TopicNameIndex {
 public:
  TopicNameIndex(Store *store, bool normalize);
  ~TopicNameIndex();

  // Add/update names for topic.
  void Update(const Frame &topic, bool ids);

  // Delete names for topic.
  void Delete(const Frame &topic);

  // Search for topics with matching names.
  void Search(const string &query, int limit, int flags, Handles *matches);

  // Find (first) full match.
  Handle Find(Text name);

 private:
  // Rebuild search index.
  void Rebuild();

  struct TopicName {
    char *name;
    Handle topic;
    TopicName *next;
  };

  static TopicName *AddName(TopicName *t, Handle topic, const string &name) {
    TopicName *tn = new TopicName();
    tn->name = strdup(name.c_str());
    tn->topic = topic;
    tn->next = t;
    return tn;
  }

  // Store for topics.
  Store *store_;

  // Normalization of names.
  bool normalize_;

  // Topics with linked list of aliases.
  HandleMap<TopicName *> topics_;

  // Names sorted by normalized name.
  std::vector<TopicName *> names_;
};

// Collaboration protocol packet reader.
class CollabReader {
 public:
  CollabReader(const uint8 *packet, size_t size)
    : packet_(packet, size), stream_(packet_), input_(&stream_) {}

  // Read varint-encoded integer from packet. Return -1 on error.
  int ReadInt() {
    uint32 value;
    if (!input_.ReadVarint32(&value)) return -1;
    return value;
  }

  // Read variable-size string from packet. Return empty on error.
  string ReadString() {
    string value;
    if (!input_.ReadVarString(&value)) value.clear();
    return value;
  }

  // Read SLING objects from packet.
  Object ReadObjects(Store *store) {
    Decoder decoder(store, &input_, false);
    return decoder.DecodeAll();
  }

  // Parse SLING objects from packet.
  bool ParseObjects(Store *store, Handles *result) {
    if (input_.Peek() == WIRE_BINARY_MARKER) {
      Decoder decoder(store, &input_);
      while (!decoder.done()) {
        Object obj = decoder.Decode();
        if (obj.IsError()) return false;
        if (obj.IsArray()) {
          Array list = obj.AsArray();
          for (int i = 0; i < list.length(); ++i) result->add(list.get(i));
        } else {
          result->add(obj.handle());
        }
      }
    } else {
      Reader reader(store, &input_);
      while (!reader.done()) {
        Object obj = reader.Read();
        if (obj.IsError()) return false;
        if (obj.IsArray()) {
          Array list = obj.AsArray();
          for (int i = 0; i < list.length(); ++i) result->add(list.get(i));
        } else {
          result->add(obj.handle());
        }
      }
    }
    return true;
  }

  // Original packet.
  const Slice &packet() const { return packet_; }

 private:
  // Original data packet.
  Slice packet_;

  // Packet input stream.
  ArrayInputStream stream_;

  // Input stream handler.
  Input input_;
};

// Collaboration protocol packet writer.
class CollabWriter {
 public:
  CollabWriter() : output_(&stream_) {}

  // Write varint-encoded integer to packet.
  void WriteInt(int value) {
    output_.WriteVarint32(value);
  }

  // Write variable-size string to packet.
  void WriteString(Text str) {
    output_.WriteVarString(str);
  }

  // Write raw data to output.
  void Write(Slice buffer) {
    output_.Write(buffer.data(), buffer.size());
  }

  // Send packet on websocket.
  void Send(WebSocket *ws) {
    output_.Flush();
    Slice packet = stream_.data();
    ws->Send(packet.data(), packet.size());
  }

  Output *output() { return &output_; }
  Slice packet() {
    output_.Flush();
    return stream_.data();
  }

 private:
  // Packet output stream.
  ArrayOutputStream stream_;

  // Output stream handler.
  Output output_;
};

// A collaboration case is a shared case managed by the collaboration server.
class CollabCase {
 public:
  CollabCase()
    : store_(commons),
      index_(&store_, true),
      idindex_(&store_, false) {}
  CollabCase(int caseid)
    : store_(commons),
      index_(&store_, true),
      idindex_(&store_, false),
      caseid_(caseid) {}

  // Read case file from input packet.
  bool Parse(CollabReader *reader);

  // Encode case to output packet.
  void EncodeCase(CollabWriter *writer);

  // Return case id.
  int caseid() const { return caseid_; }

  // Return case author.
  Handle author() const { return author_; }

  // Get main author id for case.
  Text Author() const { return store_.FrameId(author_); }

  // Add participant.
  void AddParticipant(const string &id, const string &credentials);

  // Login user.
  bool Login(CollabClient *client,
             const string &id,
             const string &credentials);

  // Logout user.
  void Logout(CollabClient *client);

  // Invite participant and return invite key.
  string Invite(const string &id);

  // Join collaboration using invite key.
  string Join(const string &id, const string &key);

  // Return new topic id.
  int NewTopicId();

  // Update collaboration.
  bool Update(CollabReader *reader);

  // Import topics into collaboration.
  int Import(CollabReader *reader);

  // Redirect all reference for topic to another.
  void Redirect(CollabReader *reader);

  // Search for matching topics in collaboration.
  Array Search(CollabReader *reader);

  // Share/publish collaboration case.
  bool Share(bool share, bool publish, string *timestamp);

  // Broadcast packet to clients. Do not send packet to source.
  void Broadcast(CollabClient *source, const Slice &packet);

  // Send pings to clients to keep connections alive.
  void SendKeepAlivePings();

  // Read case from file.
  bool ReadCase();

  // Read participants from file.
  bool ReadParticipants();

  // Write participants to file.
  void WriteParticipants();

  // Flush changes to disk.
  bool Flush(bool share, string *timestamp);

  // Check for existing case.
  static bool Exists(int caseid) {
    return File::Exists(CaseFileName(caseid));
  }

  // Collaboration store.
  Store *store() { return &store_; }

  // Topic redirect index.
  TopicNameIndex *idindex() { return &idindex_; }

 private:
  // Return case filename.
  static string CaseFileName(int caseid) {
    return FLAGS_datadir + "/" + std::to_string(caseid) + ".sling";
  }

  // Return case filename.
  static string UserFileName(int caseid) {
    return FLAGS_datadir + "/" + std::to_string(caseid) + ".access";
  }

  // Check if user is a participant.
  bool IsParticipant(const string &id);

  // Write case to file.
  void WriteCase();

  // Serialize collaboration case.
  void Serialize(Encoder *encoder, bool lazy = false);

  // Case store for collaboration.
  Store store_;

  // Topic name search index.
  TopicNameIndex index_;

  // Redirected topic id search index.
  TopicNameIndex idindex_;

  // Case file.
  Frame casefile_;

  // Case id.
  int caseid_ = 0;

  // Case author.
  Handle author_ = Handle::nil();

  // Case topics.
  Array topics_;

  // Case folders.
  Frame folders_;

  // Folder-less topics are sent on demand to the client.
  bool lazyload_ = false;

  // Whether there are changes that have not been written to disk.
  bool dirty_ = false;

  // User id and credentials.
  struct User {
    User(const string &id, const string &credentials)
      : id(id), credentials(credentials) {}
    string id;
    string credentials;
  };

  // Users currently connected to collaboration.
  std::vector<CollabClient *> clients_;

  // Participants in collaboration.
  std::vector<User> participants_;

  // Users invited as participants in collaboration.
  std::vector<User> invites_;
};

// A collaboration client is a participant in a collaboration.
class CollabClient : public WebSocket {
 public:
  CollabClient(CollabService *service, SocketConnection *conn)
    : WebSocket(conn),
      service_(service) {}

  ~CollabClient() {
    if (collab_) {
      LOG(INFO) << "Logout user " << userid_
                << " from case #" << collab_->caseid();
      MutexLock lock(&mu);
      collab_->Logout(this);
    }
  }

  void Lock() override {
    mu.Lock();
  }

  void Unlock() override {
    mu.Unlock();
  }

  // Receive packets from web socket.
  void Receive(const uint8 *data, uint64 size, bool binary) override;

  // Create new collaboration.
  void Create(CollabReader *reader);

  // Invite participant to collaborate.
  void Invite(CollabReader *reader);

  // Join collaboration.
  void Join(CollabReader *reader);

  // Log-in user to collaboration.
  void Login(CollabReader *reader);

  // Get new topic id.
  void NewId(CollabReader *reader);

  // Update collaboration.
  void Update(CollabReader *reader);

  // Flush collaboration to disk.
  void Flush(CollabReader *reader);

  // Share/publish collaboration.
  void Share(CollabReader *reader);

  // Bulk import topics into collaboration.
  void Import(CollabReader *reader);

  // Search for matching topics in collaboration.
  void Search(CollabReader *reader);

  // Retrieve topics from collaboration.
  void Topics(CollabReader *reader);

  // Retrieve topic labels from collaboration.
  void Labels(CollabReader *reader);

  // Redirect references from one topic to another.
  void Redirect(CollabReader *reader);

  // Send error message to client.
  void Error(const char *message);

 private:
  // Collaboration service.
  CollabService *service_;

  // Current collaboration for client.
  CollabCase *collab_ = nullptr;

  // Collaboration user id.
  string userid_;
};

// A collaboration service manages a number of collaboration cases with
// clients updating and monitoring live changes.
class CollabService {
 public:
  CollabService() {
    // Start checkpoint monitor.
    monitor_.SetJoinable(true);
    monitor_.Start();
  }

  ~CollabService() {
    // Stop monitor thread.
    terminate_ = true;
    notifications_.put(nullptr);
    monitor_.Join();

    // Flush changes to disk.
    Flush(false);
  }

  // Register collaboration service in HTTP server.
  void Register(HTTPServer *http) {
    http->Register("/collab", this, &CollabService::Process);
  }

  // Process HTTP websocket requests.
  void Process(HTTPRequest *request, HTTPResponse *response);

  // Add case to collaboration.
  void Add(CollabCase *collab);

  // Send notification to other participants.
  void Notify(CollabCase *collab,
              CollabClient *source,
              const Slice &packet);

  // Find case.
  CollabCase *FindCase(int caseid);

  // Re-read data from disk.
  void Refresh();

 private:
  void Monitor();

  void Flush(bool notify);

  void SendKeepAlivePings();

  // Active collaboration cases.
  std::vector<CollabCase *> collaborations_;

  // Notification queue.
  struct Message {
    Message(CollabCase *collab,
            CollabClient *source,
            const Slice &packet) : collab(collab), source(source) {
      size = packet.size();
      message = static_cast<char *>(malloc(size));
      memcpy(message, packet.data(), size);
    }

    ~Message() { free(message); }

    const Slice packet() { return Slice(message, size); }

    CollabCase *collab;
    CollabClient *source;
    char *message;
    size_t size;
  };

  Queue<Message *> notifications_;

  // Monitor thread for distributing notifications and flushing changes to disk.
  ClosureThread monitor_{[&]() { Monitor(); }};

  // Termination flag.
  bool terminate_ = false;
};

TopicNameIndex::TopicNameIndex(Store *store, bool normalize) {
  store_ = store;
  normalize_ = normalize;
}

TopicNameIndex::~TopicNameIndex() {
  for (auto &it : topics_) {
    TopicName *t = it.second;
    while (t != nullptr) {
      TopicName *next = t->next;
      free(t->name);
      free(t);
      t = next;
    }
  }
}

void TopicNameIndex::Update(const Frame &topic, bool ids) {
  // Delete exsting names for topic.
  TopicName *t = topics_[topic.handle()];
  while (t != nullptr) {
    TopicName *next = t->next;
    free(t->name);
    free(t);
    t = next;
  }

  if (ids) {
    // Add id aliases for topic.
    for (const Slot &s : topic) {
      if (s.name == Handle::is()) {
        if (store_->IsString(s.value)) {
          String str(store_, s.value);
          if (!str.valid()) continue;
          string id = str.text().str();
          t = AddName(t, topic.handle(), id);
        } else if (store_->IsPublic(s.name)) {
          string id = store_->FrameId(s.name).str();
          t = AddName(t, topic.handle(), id);
        }
      }
    }
  } else {
    // Add new names and aliases for topic.
    string normalized;
    for (const Slot &s : topic) {
      if (s.name == n_name || s.name == n_alias ||
          s.name == n_birth_name || s.name == n_married_name) {
        String str(store_, s.value);
        if (!str.valid()) continue;
        Text name = str.text();
        if (normalize_) {
          UTF8::Normalize(name.data(), name.size(), NORMALIZE_DEFAULT,
                          &normalized);
        } else {
          normalized = name.str();
        }
        t = AddName(t, topic.handle(), normalized);
      }
    }
  }
  topics_[topic.handle()] = t;

  // Clear search index, so it will be rebuild for the next seach.
  names_.clear();
}

void TopicNameIndex::Delete(const Frame &topic) {
  // Delete exsting names for topic.
  TopicName *t = topics_[topic.handle()];
  while (t != nullptr) {
    TopicName *next = t->next;
    free(t->name);
    free(t);
    t = next;
  }

  // Remove topic.
  topics_.erase(topic.handle());

  // Clear search index, so it will be rebuild for the next search.
  names_.clear();
}

void TopicNameIndex::Rebuild() {
  names_.clear();
  for (auto &it : topics_) {
    TopicName *t = it.second;
    while (t != nullptr) {
      names_.push_back(t);
      t = t->next;
    }
  }

  // Sort names.
  std::sort(names_.begin(), names_.end(),
    [](const TopicName *a, const TopicName *b) {
      return strcmp(a->name, b->name) < 0;
    });
}

void TopicNameIndex::Search(const string &query, int limit, int flags,
                            Handles *matches) {
  // Rebuild seach index if needed.
  if (names_.empty()) Rebuild();

  // Normalize query.
  string normalized;
  if (normalize_) {
    UTF8::Normalize(query.data(), query.size(), NORMALIZE_DEFAULT, &normalized);
  } else {
    normalized = query;
  }
  Text normalized_query(normalized);

  if (flags & CS_KEYWORD) {
    // Find substring matches.
    for (TopicName *tn : names_) {
      if (matches->size() > limit) break;
      if (strstr(tn->name, normalized.c_str())) {
        matches->push_back(tn->topic);
      }
    }
  } else {
    // Find first name that is greater than or equal to the query.
    int lo = 0;
    int hi = names_.size() - 1;
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      const TopicName *tn = names_[mid];
      if (tn->name < normalized_query) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }

    // Find all names matching the prefix. Stop if we hit the limit.
    int index = lo;
    while (index < names_.size()) {
      // Check if we have reached the limit.
      if (matches->size() > limit) break;

      // Stop if the current name does not match.
      const TopicName *tn = names_[index];
      Text name(tn->name);
      if (flags & CS_FULL) {
        if (name != normalized_query) break;
      } else {
        if (!name.starts_with(normalized_query)) break;
      }

      // Add match.
      matches->push_back(tn->topic);

      index++;
    }
  }
}

Handle TopicNameIndex::Find(Text name) {
  // Rebuild seach index if needed.
  if (names_.empty()) Rebuild();

  // Find match.
  int lo = 0;
  int hi = names_.size() - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const TopicName *tn = names_[mid];
    if (tn->name < name) {
      lo = mid + 1;
    } else if (tn->name > name) {
      hi = mid - 1;
    } else {
      return tn->topic;
    }
  }
  return Handle::nil();
}

bool CollabCase::Parse(CollabReader *reader) {
  casefile_ = reader->ReadObjects(&store_).AsFrame();
  if (casefile_.IsNil()) return false;

  // Get case id.
  caseid_ = casefile_.GetInt(n_caseid);
  if (caseid_ == 0) return false;

  // Get main author for case.
  Frame main = casefile_.GetFrame(n_main);
  if (!main.valid()) return false;
  author_ = main.GetHandle(n_author);
  if (author_.IsNil()) return false;

  // Get topics and folders.
  topics_ = casefile_.Get(n_topics).AsArray();
  folders_ = casefile_.GetFrame(n_folders);
  if (!topics_.valid() || !folders_.valid()) return false;
  lazyload_ = main.GetBool(n_lazyload);

  if (lazyload_) {
    // Add topic names to seach index.
    for (int i = 0; i < topics_.length(); ++i) {
      Frame topic(&store_, topics_.get(i));
      index_.Update(topic, false);
      idindex_.Update(topic, true);
    }
  }

  dirty_ = true;
  return true;
}

void CollabCase::EncodeCase(CollabWriter *writer) {
  Encoder encoder(&store_, writer->output(), false);
  Serialize(&encoder, lazyload_);
}

void CollabCase::AddParticipant(const string &id, const string &credentials) {
  participants_.emplace_back(id, credentials);
}

bool CollabCase::Login(CollabClient *client,
                       const string &id,
                       const string &credentials) {
  // Check user access.
  bool valid = false;
  for (const User &user : participants_) {
    if (user.id == id && user.credentials == credentials) {
      valid = true;
      break;
    }
  }
  if (!valid) return false;

  // Check that user is still a participant.
  if (!IsParticipant(id)) return false;

  // Add client as listener.
  clients_.push_back(client);
  return true;
}

void CollabCase::Logout(CollabClient *client) {
  auto it = std::find(clients_.begin(), clients_.end(), client);
  if (it != clients_.end()) {
    clients_.erase(it);
  }
}

string CollabCase::Invite(const string &id) {
  // Check that user is a participant.
  if (!IsParticipant(id)) return "";

  // Generate new invite key.
  string key = RandomKey();
  invites_.emplace_back(id, key);
  return key;
}

string CollabCase::Join(const string &id, const string &key) {
  // Check that user is a participant.
  if (!IsParticipant(id)) return "";

  // Check that user has been invited.
  bool valid = false;
  for (auto it = invites_.begin(); it != invites_.end(); ++it) {
    if (it->id == id && it->credentials == key) {
      valid = true;
      if (FLAGS_onetime_invite) {
        // Remove invite so it cannot be used again.
        invites_.erase(it);
      }
      break;
    }
  }
  if (!valid) return "";

  // Check for existing credentials.
  for (const User &user : participants_) {
    if (user.id == id) {
      return user.credentials;
    }
  }

  // Generate new credentials.
  string credentials = RandomKey();
  participants_.emplace_back(id, credentials);
  return credentials;
}

int CollabCase::NewTopicId() {
  int next = casefile_.GetInt(n_next);
  casefile_.Set(n_next, Handle::Integer(next + 1));
  return next;
}

bool CollabCase::Update(CollabReader *reader) {
  int type = reader->ReadInt();
  switch (type) {
    case CCU_TOPIC: {
      // Get new topic.
      Frame topic = reader->ReadObjects(&store_).AsFrame();
      if (!topic.valid()) return false;

      // Check for new topic.
      if (!topics_.Contains(topic.handle())) {
        topics_.Append(topic.handle());
        LOG(INFO) << "Case #" << caseid_ << " topic new " << topic.Id();
      } else {
        LOG(INFO) << "Case #" << caseid_ << " topic update " << topic.Id();
      }
      if (lazyload_) {
        index_.Update(topic, false);
        idindex_.Update(topic, true);
      }
      dirty_ = true;
      break;
    }

    case CCU_TOPICS: {
      // Get new/updated topics.
      Array topics = reader->ReadObjects(&store_).AsArray();
      if (!topics.valid()) return false;

      for (int i = 0; i < topics.length(); ++i) {
        Frame topic(&store_, topics.get(i));
        if (topic.invalid()) return false;

        // Check for new topic.
        if (!topics_.Contains(topic.handle())) {
          topics_.Append(topic.handle());
          LOG(INFO) << "Case #" << caseid_ << " topic new " << topic.Id();
        } else {
          LOG(INFO) << "Case #" << caseid_ << " topic update " << topic.Id();
        }
        if (lazyload_) {
          index_.Update(topic, false);
          idindex_.Update(topic, true);
        }
      }
      dirty_ = true;
      break;
    }

    case CCU_FOLDER: {
      // Get folder name and topic list.
      string folder = reader->ReadString();
      Array topcis = reader->ReadObjects(&store_).AsArray();

      // Update topic list for folder.
      for (int i = 0; i < folders_.size(); ++i) {
        Slot &s = folders_.slot(i);
        if (String(&store_, s.name).equals(folder)) {
          s.value = topcis.handle();
          LOG(INFO) << "Case #" << caseid_
                    << " folder " << folder << " updated";
          break;
        }
      }
      dirty_ = true;
      break;
    }

    case CCU_FOLDERS: {
      // Get folder list.
      Array folders = reader->ReadObjects(&store_).AsArray();

      // Make map of existing folders.
      std::unordered_map<string, Handle> folder_map;
      for (const Slot &s : folders_) {
        String name(&store_, s.name);
        folder_map[name.value()] = s.value;
      }

      // Build new folder list.
      Builder builder(folders_);
      builder.Reset();
      for (int i = 0; i < folders.length(); ++i) {
        String name(&store_, folders.get(i));
        auto f = folder_map.find(name.value());
        if (f != folder_map.end()) {
          builder.Add(name, f->second);
        } else {
          builder.Add(name, store_.AllocateArray(0));
        }
      }
      builder.Update();
      LOG(INFO) << "Case #" << caseid_ << " folders updated";
      dirty_ = true;
      break;
    }

    case CCU_DELETE: {
      // Get topic id.
      string topicid = reader->ReadString();
      Handle topic = store_.LookupExisting(topicid);
      if (topic.IsNil() || !topics_.Erase(topic)) {
        LOG(ERROR) << "Case #" << caseid_ << " unknown topic " << topicid;
      } else {
        if (lazyload_) {
         Frame f(&store_, topic);
          index_.Delete(f);
          idindex_.Delete(f);
        }
        LOG(INFO) << "Case #" << caseid_
                  << " topic " << topicid << " deleted";
        dirty_ = true;
      }
      break;
    }

    case CCU_RENAME: {
      // Get old and new folder names.
      string oldname = reader->ReadString();
      string newname = reader->ReadString();

      // Rename folder.
      for (int i = 0; i < folders_.size(); ++i) {
        String name(&store_, folders_.name(i));
        if (name.value() == oldname) {
          folders_.slot(i).name = String(&store_, newname).handle();
          dirty_ = true;
          LOG(INFO) << "Case #" << caseid_ << " folder " << oldname
                    << " renamed to " << newname;
          break;
        }
      }
      break;
    }

    default:
      LOG(ERROR) << "Invalid case update type " << type;
  }

  return true;
}

void CollabCase::SendKeepAlivePings() {
  time_t now = time(nullptr);
  for (CollabClient *client : clients_) {
    if (now - client->last() > FLAGS_ping) {
      client->Ping("keep-alive", 10);
    }
  }
}

void CollabCase::Broadcast(CollabClient *source, const Slice &packet) {
  for (CollabClient *client : clients_) {
    if (client != source) {
      client->Send(packet);
    }
  }
}

int CollabCase::Import(CollabReader *reader) {
  string folder = reader->ReadString();
  Handles topics(&store_);
  if (!reader->ParseObjects(&store_, &topics)) return -1;

  // Assign topic ids to imported topic.
  for (Handle t : topics) {
    int id =   NewTopicId();
    string topicid = StrCat("t/", caseid_, "/", id);
    Builder b(&store_);
    b.AddId(topicid);
    b.AddFrom(t);
    b.Update(t);
  }

  // Add topics to case.
  topics_.Append(topics);

  // Broadcast new topics to all paritcipants.
  CollabWriter writer;
  writer.WriteInt(COLLAB_UPDATE);
  writer.WriteInt(CCU_TOPIC);
  Encoder encoder(&store_, writer.output(), false);
  for (Handle t : topics) encoder.Encode(t);
  encoder.Encode(Array(&store_, topics));
  collabd->Notify(this, nullptr, writer.packet());

  // Add imported topics to folder (optional).
  if (!folder.empty()) {
    for (int i = 0; i < folders_.size(); ++i) {
      Slot &s = folders_.slot(i);
      if (String(&store_, s.name).equals(folder)) {
        // Add new topics to folder.
        Array folder_topics(&store_, s.value);
        folder_topics.Append(topics);

        // Broadcast folder update.
        CollabWriter writer;
        writer.WriteInt(COLLAB_UPDATE);
        writer.WriteInt(CCU_FOLDER);
        writer.WriteString(folder);
        Encoder encoder(&store_, writer.output(), false);
        encoder.Encode(folder_topics);
        collabd->Notify(this, nullptr, writer.packet());
        break;
      }
    }
  }

  LOG(INFO) << "Imported " << topics.size()
            << " topics into case #" << caseid_;
  dirty_ = true;
  return topics.size();
}

void CollabCase::Redirect(CollabReader *reader) {
  // Reading source and target topics ids.
  string sourceid = reader->ReadString();
  string targetid = reader->ReadString();
  LOG(INFO) << "Case #" << caseid_ << " redirect "
            << sourceid << " to " << targetid;

  Handle source = store_.LookupExisting(sourceid);
  if (source.IsNil()) return;
  Handle target = store_.LookupExisting(targetid);

  // Redirect source to target.
  Handles updates(&store_);
  for (int i = 0; i < topics_.length(); ++i) {
    Handle t = topics_.get(i);
    if (t == target) continue;
    FrameDatum *topic = store_.GetFrame(t);
    bool updated = false;
    for (Slot *s = topic->begin(); s < topic->end(); ++s) {
      if (s->value == source) {
        if (target.IsNil()) {
          s->value = store_.AllocateString(targetid);
        } else {
          s->value = target;
        }
        updated = true;
      } else if (s->name == Handle::is()) {
        if (store_.IsString(s->value)) {
          StringDatum *redirect = store_.GetString(s->value);
          if (redirect->equals(sourceid)) {
            s->value = store_.AllocateString(targetid);
            updated = true;
          }
        }
      } else if (store_.IsFrame(s->value) && store_.IsAnonymous(s->value)) {
        FrameDatum *qualifer = store_.GetFrame(s->value);
        for (Slot *qs = qualifer->begin(); qs < qualifer->end(); ++qs) {
          if (qs->value == source) {
            if (target.IsNil()) {
              qs->value = store_.AllocateString(targetid);
            } else {
              qs->value = target;
            }
            updated = true;
          }
        }
      }
    }
    if (updated) updates.push_back(t);
  }

  // Broadcast topic updates to all paritcipants.
  if (!updates.empty()) {
    CollabWriter writer;
    writer.WriteInt(COLLAB_UPDATE);
    writer.WriteInt(CCU_TOPIC);
    Encoder encoder(&store_, writer.output(), false);
    for (Handle t : updates) encoder.Encode(t);
    encoder.Encode(Array(&store_, updates));
    collabd->Notify(this, nullptr, writer.packet());
    dirty_ = true;
  }
}

Array CollabCase::Search(CollabReader *reader) {
  string query = reader->ReadString();
  int limit = reader->ReadInt();
  int flags = reader->ReadInt();
  Handles hits(&store_);

  // Check for matching topic id.
  Handle idmatch = store_.LookupExisting(query);
  if (!idmatch.IsNil() && topics_.Contains(idmatch)) hits.push_back(idmatch);

  // Check for matching redirects.
  if (lazyload_) {
    idindex_.Search(query, limit, CS_FULL, &hits);
  }

  // Search topic names and aliases for  matches.
  index_.Search(query, limit, flags, &hits);
  Handles matches(&store_);

  // Return matches.
  for (Handle h : hits) {
    Frame hit(&store_, h);
    if (!hit.valid()) continue;
    Text id = hit.Id();
    Text name = hit.GetText(n_name);
    Text description = hit.GetText(n_description);
    Builder match(&store_);
    if (!id.empty()) match.Add(n_ref, id);
    if (!name.empty()) match.Add(n_name, name);
    if (!description.empty()) match.Add(n_description, description);
    matches.add(match.Create().handle());
  }

  LOG(INFO) << "Case #" << caseid_ << " search for '" << query << "', "
            << matches.size() << " hits";

  return Array(&store_, matches);
}

bool CollabCase::Share(bool share, bool publish, string *timestamp) {
  // Connect to case database if not already done.
  if (!pubdb.connected()) {
    if (FLAGS_pubdb.empty()) {
      LOG(WARNING) << "No case database for case sharing";
      return false;
    }
    Status st = pubdb.Connect(FLAGS_pubdb, "collabd");
    if (!st.ok()) {
      LOG(ERROR) << "Error connecting to case database: " << st;
      return false;
    }
  }

  if (share || publish) {
    // Flush changes to disk.
    casefile_.Set(n_share, share);
    casefile_.Set(n_publish, publish);
    Flush(true, timestamp);

    // Serialize case.
    ArrayOutputStream stream;
    Output output(&stream);
    Encoder encoder(&store_, &output);
    Serialize(&encoder, false);
    output.Flush();

    // Write case to case database.
    string key = StrCat(caseid_);
    DBRecord record;
    record.key = key;
    record.version = time(nullptr);
    record.value = stream.data();

    Status st = pubdb.Put(&record);
    if (!st.ok()) {
      LOG(ERROR) << "Error writing to case database: " << st;
      return false;
    }

    LOG(INFO) << (publish ? "Published" : "Shared") << " case #" << caseid_;
  } else {
    LOG(INFO) << "Unshare case #" << caseid_;
    casefile_.Set(n_share, false);
    casefile_.Set(n_publish, false);
    Flush(true, nullptr);
    string key = StrCat(caseid_);
    Status st = pubdb.Delete(key);
    if (!st.ok()) {
      LOG(ERROR) << "Error deleting case from database: " << st;
      return false;
    }
  }

  return true;
}

bool CollabCase::ReadCase() {
  // Open case file.
  File *f;
  Status st = File::Open(CaseFileName(caseid_), "r", &f);
  if (!st.ok()) {
    LOG(ERROR) << "Error opening case# " << caseid() << ": " << st;
    return false;
  }

  // Decode case.
  FileInputStream stream(f);
  Input input(&stream);
  Decoder decoder(&store_, &input);
  casefile_ = decoder.DecodeAll().AsFrame();
  if (casefile_.IsNil() || casefile_.IsError()) return false;

  // Get main author for case.
  Frame main = casefile_.GetFrame(n_main);
  if (!main.valid()) return false;
  author_ = main.GetHandle(n_author);
  if (author_.IsNil()) return false;

  // Get topics and folders.
  topics_ = casefile_.Get(n_topics).AsArray();
  folders_ = casefile_.GetFrame(n_folders);
  lazyload_ = casefile_.GetBool(n_lazyload);

  if (lazyload_) {
    // Add topic names to seach index.
    for (int i = 0; i < topics_.length(); ++i) {
      Frame topic(&store_, topics_.get(i));
      index_.Update(topic, false);
      idindex_.Update(topic, true);
    }
  }

  dirty_ = false;
  return true;
}

bool CollabCase::ReadParticipants() {
  // Read user file.
  string content;
  Status st = File::ReadContents(UserFileName(caseid_), &content);
  if (!st) return false;

  // Parse users.
  participants_.clear();
  for (Text &line : Text(content).split('\n')) {
    auto fields = line.split(' ');
    CHECK_EQ(fields.size(), 2);
    auto id = fields[0].trim();
    auto credentials = fields[1].trim();
    participants_.emplace_back(id.str(), credentials.str());
  }

  return true;
}

void CollabCase::WriteParticipants() {
  File *f = File::OpenOrDie(UserFileName(caseid_), "w");
  for (const User &user : participants_) {
    f->WriteLine(user.id + " " + user.credentials);
  }
  f->Close();
}

bool CollabCase::Flush(bool share, string *timestamp) {
  if (!share && !dirty_) {
    if (timestamp) *timestamp = casefile_.GetString(n_modified);
    return false;
  }

  // Update modification timestamp in case.
  time_t now = time(nullptr);
  char modtime[128];
  struct tm tm;
  gmtime_r(&now, &tm);
  strftime(modtime, sizeof modtime, "%Y-%m-%dT%H:%M:%SZ", &tm);
  casefile_.Set(n_modified, modtime);
  if (share) casefile_.Set(n_shared, modtime);

  // Write case to file.
  WriteCase();
  dirty_ = false;
  if (timestamp) *timestamp = modtime;
  int secs = time(nullptr) - now;
  LOG(INFO) << "Saved case #" << caseid_ << " (" << secs << " secs)";
  return true;
}

bool CollabCase::IsParticipant(const string &id) {
  Handle user = store_.LookupExisting(id);
  if (user.IsNil()) return false;
  Frame main = casefile_.GetFrame(n_main);
  if (!main.valid()) return false;
  for (const Slot &s : main) {
    if (s.name == n_participant || s.name == n_author) {
      if (s.value == user) return true;
    }
  }
  return false;
}

void CollabCase::WriteCase() {
  FileOutputStream stream(CaseFileName(caseid_));
  Output output(&stream);
  Encoder encoder(&store_, &output);
  Serialize(&encoder);
}

void CollabCase::Serialize(Encoder *encoder, bool lazy) {
  if (lazy) {
    // Only serialize topics in folders in lazy mode.
    HandleSet seen;
    for (int i = 0; i < folders_.size(); ++i) {
      Slot &s = folders_.slot(i);
      Array topics(&store_, s.value);
      for (int j = 0; j < topics.length(); ++j) {
        Handle topic = topics.get(j);
        if (seen.has(topic)) continue;
        encoder->Encode(topic);
        seen.add(topic);
      }
    }
  } else {
    for (int i = 0; i < topics_.length(); ++i) {
      encoder->Encode(topics_.get(i));
    }
  }
  encoder->Encode(casefile_);
}

void CollabClient::Receive(const uint8 *data, uint64 size, bool binary) {
  CollabReader reader(data, size);
  int op = reader.ReadInt();
  switch (op) {
    case COLLAB_CREATE: Create(&reader); break;
    case COLLAB_INVITE: Invite(&reader); break;
    case COLLAB_JOIN: Join(&reader); break;
    case COLLAB_LOGIN: Login(&reader); break;
    case COLLAB_NEWID: NewId(&reader); break;
    case COLLAB_UPDATE: Update(&reader); break;
    case COLLAB_FLUSH: Flush(&reader); break;
    case COLLAB_IMPORT: Import(&reader); break;
    case COLLAB_SEARCH: Search(&reader); break;
    case COLLAB_TOPICS: Topics(&reader); break;
    case COLLAB_LABELS: Labels(&reader); break;
    case COLLAB_REDIRECT: Redirect(&reader); break;
    case COLLAB_SHARE: Share(&reader); break;
    default:
      LOG(ERROR) << "Invalid collab op: " << op;
  }
}

void CollabClient::Create(CollabReader *reader) {
  // Make sure client is not already connected to collaboration.
  if (collab_ != nullptr) {
    Error("already connected to a collaboration");
    return;
  }

  // Receive initial case for collaboration.
  CollabCase *collab = new CollabCase();
  if (!collab->Parse(reader)) {
    Error("invalid case format");
    delete collab;
    return;
  }

  // Make sure case is not already registered.
  if (CollabCase::Exists(collab->caseid())) {
    Error("case is already registered as a collaboration");
    delete collab;
    return;
  }

  // Add user as participant in collaboration.
  string userid = collab->Author().str();
  if (userid.find(' ') != -1) {
    Error("invalid user id");
    delete collab;
    return;
  }
  string credentials = RandomKey();
  collab->AddParticipant(userid, credentials);

  // Add collaboration to service.
  service_->Add(collab);

  // Flush to disk.
  collab->WriteParticipants();
  collab->Flush(false, nullptr);

  // Return reply which signals to the client that the collaboration server
  // has taken ownership of the case.
  CollabWriter writer;
  writer.WriteInt(COLLAB_CREATE);
  writer.WriteString(credentials);
  writer.Send(this);

  LOG(INFO) << "Created new collaboration for case #" << collab->caseid()
            << " author " << collab->Author();
}

void CollabClient::Invite(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Receive <user>.
  string userid = reader->ReadString();
  LOG(INFO) << "Invite " << userid << " to case #" << collab_->caseid();

  // Generate invite key for new participant.
  string key = collab_->Invite(userid);
  if (key.empty()) {
    Error("user is not a collaboration participant");
    return;
  }

  // Return new invite key.
  CollabWriter writer;
  writer.WriteInt(COLLAB_INVITE);
  writer.WriteString(key);
  writer.Send(this);
}

void CollabClient::Join(CollabReader *reader) {
  // Receive <caseid> <user> <invite key>.
  int caseid = reader->ReadInt();
  string userid = reader->ReadString();
  string key = reader->ReadString();
  LOG(INFO) << "User " << userid << " joining case #" << caseid;

  // Find case.
  CollabCase *collab = service_->FindCase(caseid);
  if (collab == nullptr) {
    Error("unknown collaboration");
    return;
  }

  // Join collaboration.
  string credentials = collab->Join(userid, key);
  if (credentials.empty()) {
    LOG(WARNING) << "Joining case #" << caseid << " denied for " << userid;
    Error("user not invited to collaborate");
    return;
  }
  collab->WriteParticipants();

  // Return credentials for logging into collaboration.
  CollabWriter writer;
  writer.WriteInt(COLLAB_JOIN);
  writer.WriteString(credentials);
  writer.Send(this);
}

void CollabClient::Login(CollabReader *reader) {
  // Make sure client is not already connected to collaboration.
  if (collab_ != nullptr) {
    Error("already connected to a collaboration");
    return;
  }

  // Receive <caseid> <user> <credentials>.
  int caseid = reader->ReadInt();
  string userid = reader->ReadString();
  string credentials = reader->ReadString();
  LOG(INFO) << "Login " << userid << " to case #" << caseid;

  // Get case.
  collab_ = service_->FindCase(caseid);
  if (collab_ == nullptr) {
    Error("unknown collaboration");
    return;
  }

  // Log into collaboration to send and receive updates.
  if (!collab_->Login(this, userid, credentials)) {
    LOG(WARNING) << "Access to case #" << caseid << " denied for " << userid;
    Error("access denied");
    collab_ = nullptr;
    return;
  }
  userid_ = userid;

  // Return case.
  CollabWriter writer;
  writer.WriteInt(COLLAB_LOGIN);
  collab_->EncodeCase(&writer);
  writer.Send(this);
}

void CollabClient::NewId(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Return new topic id.
  int next = collab_->NewTopicId();
  CollabWriter writer;
  writer.WriteInt(COLLAB_NEWID);
  writer.WriteInt(next);
  writer.Send(this);
}

void CollabClient::Update(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Update collaboration.
  if (!collab_->Update(reader)) {
    Error("invalid update");
    return;
  }

  // Broadcast update to all other participants.
  service_->Notify(collab_, this, reader->packet());
}

void CollabClient::Flush(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Flush collaboration.
  string modtime;
  bool saved = collab_->Flush(false, &modtime);

  // Return latest modification time.
  CollabWriter writer;
  writer.WriteInt(COLLAB_FLUSH);
  writer.WriteString(modtime);
  writer.Send(this);

  // Broadcast save.
  if (saved) {
    CollabWriter writer;
    writer.WriteInt(COLLAB_UPDATE);
    writer.WriteInt(CCU_SAVE);
    writer.WriteString(modtime);
    service_->Notify(collab_, this, writer.packet());
  }
}

void CollabClient::Import(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Import topics.
  int num_topics = collab_->Import(reader);
  if (num_topics == -1) {
    Error("error importing topics");
    return;
  }

  CollabWriter writer;
  writer.WriteInt(COLLAB_IMPORT);
  writer.WriteInt(num_topics);
  writer.Send(this);
}

void CollabClient::Search(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Search for matching topics in collaboration.
  Array hits = collab_->Search(reader);

  CollabWriter writer;
  writer.WriteInt(COLLAB_SEARCH);
  Encoder encoder(collab_->store(), writer.output(), false);
  encoder.Encode(hits);
  writer.Send(this);
}

void CollabClient::Share(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }
  bool share = reader->ReadInt();
  bool publish = reader->ReadInt();

  // Share collaboration.
  string modtime;
  bool ok = collab_->Share(share, publish, &modtime);
  if (!ok) {
    Error("error sharing collaboration");
    return;
  }

  // Return modification/sharing time.
  CollabWriter writer;
  writer.WriteInt(COLLAB_SHARE);
  writer.WriteString(modtime);
  writer.Send(this);

  // Broadcast modification time.
  CollabWriter broadcast;
  broadcast.WriteInt(COLLAB_UPDATE);
  broadcast.WriteInt(CCU_SAVE);
  broadcast.WriteString(modtime);
  service_->Notify(collab_, this, broadcast.packet());
}

void CollabClient::Topics(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Reading array with proxies will resolve them.
  Store *store = collab_->store();
  Array topics = reader->ReadObjects(store).AsArray();
  if (!topics.valid()) {
    Error("invalid topic request");
    return;
  }

  // Return resolved topics.
  CollabWriter writer;
  writer.WriteInt(COLLAB_TOPICS);
  Encoder encoder(store, writer.output(), false);
  for (int i = 0; i < topics.length(); ++i) {
    Handle topic = topics.get(i);

    // Try to resolve external topic to local topic.
    if (store->IsProxy(topic)) {
      Text id = store->FrameId(topic);
      Handle local = collab_->idindex()->Find(id);
      if (local != Handle::nil()) topic = local;
    }

    encoder.Encode(topic);
  }
  writer.Send(this);
}

void CollabClient::Labels(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  // Reading array with proxies will resolve them.
  Store *store = collab_->store();
  Array topics = reader->ReadObjects(store).AsArray();
  if (!topics.valid()) {
    Error("invalid topic request");
    return;
  }

  // Return stubs for topics.
  CollabWriter writer;
  writer.WriteInt(COLLAB_LABELS);
  Handles stubs(store);
  Encoder encoder(collab_->store(), writer.output(), false);
  for (int i = 0; i < topics.length(); ++i) {
    Frame topic(store, topics.get(i));
    if (!topic.valid()) continue;
    Text name = topic.GetText(n_name);
    Builder match(store);
    match.Add(n_topic, topic);
    if (!name.empty()) match.Add(n_name, name);
    stubs.add(match.Create().handle());
  }
  Array results(store, stubs);
  encoder.Encode(results.handle());
  writer.Send(this);
}

void CollabClient::Redirect(CollabReader *reader) {
  // Make sure client is logged into case.
  if (collab_ == nullptr) {
    Error("user not logged in");
    return;
  }

  collab_->Redirect(reader);
}

void CollabClient::Error(const char *message) {
  CollabWriter writer;
  writer.WriteInt(COLLAB_ERROR);
  writer.WriteString(message);
  writer.Send(this);
}

void CollabService::Process(HTTPRequest *request, HTTPResponse *response) {
  CollabClient *client = new CollabClient(this, request->conn());
  if (!WebSocket::Upgrade(client, request, response)) {
    delete client;
    response->SendError(404);
    return;
  }
}

void CollabService::Add(CollabCase *collab) {
  collaborations_.push_back(collab);
}

void CollabService::Notify(CollabCase *collab,
                           CollabClient *source,
                           const Slice &packet) {
  notifications_.put(new Message(collab, source, packet));
}

CollabCase *CollabService::FindCase(int caseid) {
  // Try to find case that has already been loaded.
  for (auto *collab : collaborations_) {
    if (collab->caseid() == caseid) return collab;
  }

  // Try to load case from file.
  LOG(INFO) << "Loading case #" << caseid;
  CollabCase *collab = new CollabCase(caseid);
  if (!collab->ReadCase() || !collab->ReadParticipants()) {
    delete collab;
    return nullptr;
  }

  // Add collaboration.
  collaborations_.push_back(collab);
  return collab;
}

void CollabService::Refresh() {
  LOG(INFO) << "Refresh collaborations from disk";
  MutexLock lock(&mu);
  for (auto *collab : collaborations_) {
    if (!collab->ReadCase() || !collab->ReadParticipants()) {
      LOG(ERROR) << "Unable to refresh case #" << collab->caseid();
    }
  }
}

void CollabService::Monitor() {
  time_t last_flush = time(nullptr);
  time_t last_ping = time(nullptr);
  for (;;) {
    // Wait for next update.
    Message *msg = notifications_.get(1000);
    if (terminate_) return;

    // Broadcast notification to participants.
    if (msg) {
      MutexLock lock(&mu);
      msg->collab->Broadcast(msg->source, msg->packet());
      delete msg;
    }

    // Flush changes to disk.
    time_t now = time(nullptr);
    if (now - last_flush >= FLAGS_flush) {
      Flush(true);
      last_flush = now;
    }

    // Send keep-alive pings to clients.
    if (now - last_ping >= FLAGS_ping) {
      SendKeepAlivePings();
      last_ping = now;
    }
  }
}

void CollabService::Flush(bool notify) {
  // Flush changes to disk.
  MutexLock lock(&mu);
  string timestamp;
  for (CollabCase *collab : collaborations_) {
    if (collab->Flush(false, &timestamp) && notify) {
      // Broadcast save.
      CollabWriter writer;
      writer.WriteInt(COLLAB_UPDATE);
      writer.WriteInt(CCU_SAVE);
      writer.WriteString(timestamp);
      Notify(collab, nullptr, writer.packet());
    }
  }
}

void CollabService::SendKeepAlivePings() {
  MutexLock lock(&mu);
  for (CollabCase *collab : collaborations_) {
    collab->SendKeepAlivePings();
  }
}

// Termination handler.
void terminate(int signum) {
  VLOG(1) << "Shutdown requested";
  if (httpd != nullptr) httpd->Shutdown();
}

// Refresh collaborations.
void refresh(int signum) {
  VLOG(1) << "Refresh collaboration";
  if (collabd != nullptr) collabd->Refresh();
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Initialize commons store.
  commons = new Store();
  names.Bind(commons);
  commons->Freeze();

  // Initialize collaboration service.
  collabd = new CollabService();

  // Install signal handlers to handle termination and refresh.
  signal(SIGTERM, terminate);
  signal(SIGINT, terminate);
  signal(SIGHUP, refresh);

  // Start HTTP server.
  LOG(INFO) << "Start HTTP server on port " << FLAGS_port;
  SocketServerOptions sockopts;
  sockopts.num_workers = FLAGS_workers;
  httpd = new HTTPServer(sockopts, FLAGS_addr.c_str(), FLAGS_port);
  collabd->Register(httpd);
  CHECK(httpd->Start());
  LOG(INFO) << "Collaboration server running";
  httpd->Wait();

  // Shut down.
  LOG(INFO) << "Shutting down HTTP server";
  delete httpd;
  httpd = nullptr;

  LOG(INFO) << "Shutting down collaboration service";
  delete collabd;
  collabd = nullptr;

  LOG(INFO) << "Done";
  return 0;
}
