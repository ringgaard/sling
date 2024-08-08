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
#include "sling/net/http-server.h"
#include "sling/net/web-sockets.h"
#include "sling/stream/input.h"
#include "sling/stream/file.h"
#include "sling/stream/memory.h"
#include "sling/string/strcat.h"
#include "sling/util/mutex.h"
#include "sling/util/queue.h"
#include "sling/util/thread.h"

using namespace sling;

DEFINE_string(addr, "", "HTTP server address");
DEFINE_int32(port, 7700, "HTTP server port");
DEFINE_int32(workers, 16, "Number of network worker threads");
DEFINE_int32(flush, 30, "Number of seconds before writing changes to disk");
DEFINE_int32(ping, 30, "Number of seconds between keep-alive pings");
DEFINE_int32(onetime_invite, false, "Invalidate invite when joining");
DEFINE_string(datadir, ".", "Data directory for collaborations");

// Collaboration protocol opcodes.
enum CollabOpcode {
  COLLAB_CREATE  = 1,    // create new collaboration
  COLLAB_DELETE  = 2,    // delete collaboration
  COLLAB_INVITE  = 3,    // invite participant to join collaboration
  COLLAB_JOIN    = 4,    // add user as participant in collaboration
  COLLAB_LOGIN   = 5,    // log-in to collaboration to send and receive updates
  COLLAB_NEWID   = 6,    // new topic id
  COLLAB_UPDATE  = 7,    // update collaboration case
  COLLAB_FLUSH   = 8,    // flush changes to disk
  COLLAB_IMPORT  = 9,    // bulk import topics

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

// Commons store with global symbols.
Store *commons;
Names names;
Name n_caseid(names, "caseid");
Name n_main(names, "main");
Name n_topics(names, "topics");
Name n_folders(names, "folders");
Name n_modified(names, "modified");
Name n_lazyload(names, "lazyload");
Name n_next(names, "next");
Name n_author(names, "P50");
Name n_participant(names, "P710");

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
  CollabCase() : store_(commons) {}
  CollabCase(int caseid) : store_(commons), caseid_(caseid) {}

  // Read case file from input packet.
  bool Parse(CollabReader *reader) {
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
    dirty_ = true;
    return true;
  }

  // Encode case to output packet.
  void EncodeCase(CollabWriter *writer) {
    Encoder encoder(&store_, writer->output(), false);
    Serialize(&encoder, lazyload_);
  }

  // Return case id.
  int caseid() const { return caseid_; }

  // Return case author.
  Handle author() const { return author_; }

  // Get main author id for case.
  Text Author() const {
    return store_.FrameId(author_);
  }

  // Add participant.
  void AddParticipant(const string &id, const string &credentials) {
    participants_.emplace_back(id, credentials);
  }

  // Login user.
  bool Login(CollabClient *client,
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

  // Logout user.
  void Logout(CollabClient *client) {
    auto it = std::find(clients_.begin(), clients_.end(), client);
    if (it != clients_.end()) {
      clients_.erase(it);
    }
  }

  // Invite participant and return invite key.
  string Invite(const string &id) {
    // Check that user is a participant.
    if (!IsParticipant(id)) return "";

    // Generate new invite key.
    string key = RandomKey();
    invites_.emplace_back(id, key);
    return key;
  }

  // Join collaboration using invite key.
  string Join(const string &id, const string &key) {
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

  // Return new topic id.
  int NewTopicId() {
    int next = casefile_.GetInt(n_next);
    casefile_.Set(n_next, Handle::Integer(next + 1));
    return next;
  }

  // Update collaboration.
  bool Update(CollabReader *reader) {
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

  // Import topics into collaboration.
  int Import(CollabReader *reader);

  // Broadcast packet to clients. Do not send packet to source.
  void Broadcast(CollabClient *source, const Slice &packet);

  // Send pings to clients to keep connections alive.
  void SendKeepAlivePings();

  // Read case from file.
  bool ReadCase() {
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
    dirty_ = false;
    return true;
  }

  // Read participants from file.
  bool ReadParticipants() {
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

  // Write participants to file.
  void WriteParticipants() {
    File *f = File::OpenOrDie(UserFileName(caseid_), "w");
    for (const User &user : participants_) {
      f->WriteLine(user.id + " " + user.credentials);
    }
    f->Close();
  }

  // Flush changes to disk.
  bool Flush(string *timestamp) {
    if (!dirty_) {
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

    // Write case to file.
    LOG(INFO) << "Save case #" << caseid_;
    WriteCase();
    dirty_ = false;
    if (timestamp) *timestamp = modtime;
    return true;
  }

  // Check for existing case.
  static bool Exists(int caseid) {
    return File::Exists(CaseFileName(caseid));
  }

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
  bool IsParticipant(const string &id) {
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

  // Write case to file.
  void WriteCase() {
    FileOutputStream stream(CaseFileName(caseid_));
    Output output(&stream);
    Encoder encoder(&store_, &output);
    Serialize(&encoder);
  }

  // Serialize collaboration case.
  void Serialize(Encoder *encoder, bool lazy = false) {
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

  // Case store for collaboration.
  Store store_;

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
  void Add(CollabCase *collab) {
    collaborations_.push_back(collab);
  }

  // Send notification to other participants.
  void Notify(CollabCase *collab,
              CollabClient *source,
              const Slice &packet) {
    notifications_.put(new Message(collab, source, packet));
  }

  // Find case.
  CollabCase *FindCase(int caseid) {
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

  // Re-read data from disk.
  void Refresh() {
    LOG(INFO) << "Refresh collaborations from disk";
    MutexLock lock(&mu);
    for (auto *collab : collaborations_) {
      if (!collab->ReadCase() || !collab->ReadParticipants()) {
        LOG(ERROR) << "Unable to refresh case #" << collab->caseid();
      }
    }
  }

 private:
  void Monitor() {
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

  void Flush(bool notify) {
    // Flush changes to disk.
    MutexLock lock(&mu);
    string timestamp;
    for (CollabCase *collab : collaborations_) {
      if (collab->Flush(&timestamp) && notify) {
        // Broadcast save.
        CollabWriter writer;
        writer.WriteInt(COLLAB_UPDATE);
        writer.WriteInt(CCU_SAVE);
        writer.WriteString(timestamp);
        Notify(collab, nullptr, writer.packet());
      }
    }
  }

  void SendKeepAlivePings() {
    MutexLock lock(&mu);
    for (CollabCase *collab : collaborations_) {
      collab->SendKeepAlivePings();
    }
  }

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

  void Receive(const uint8 *data, uint64 size, bool binary) override {
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
      default:
        LOG(ERROR) << "Invalid collab op: " << op;
    }
  }

  // Create new collaboration.
  void Create(CollabReader *reader) {
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
    collab->Flush(nullptr);

    // Return reply which signals to the client that the collaboration server
    // has taken ownership of the case.
    CollabWriter writer;
    writer.WriteInt(COLLAB_CREATE);
    writer.WriteString(credentials);
    writer.Send(this);

    LOG(INFO) << "Created new collaboration for case #" << collab->caseid()
              << " author " << collab->Author();
  }

  // Invite participant to collaborate.
  void Invite(CollabReader *reader) {
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

  // Join collaboration.
  void Join(CollabReader *reader) {
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

  // Log-in user to collaboration.
  void Login(CollabReader *reader) {
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

  // Get new topic id.
  void NewId(CollabReader *reader) {
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

  // Update collaboration.
  void Update(CollabReader *reader) {
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

  // Flush collaboration to disk.
  void Flush(CollabReader *reader) {
    // Make sure client is logged into case.
    if (collab_ == nullptr) {
      Error("user not logged in");
      return;
    }

    // Flush collaboration.
    string modtime;
    bool saved = collab_->Flush(&modtime);

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

  // Bulk import topics into collaboration.
  void Import(CollabReader *reader) {
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

  // Return error message to client.
  void Error(const char *message) {
    CollabWriter writer;
    writer.WriteInt(COLLAB_ERROR);
    writer.WriteString(message);
    writer.Send(this);
  }

 private:
  // Collaboration service.
  CollabService *service_;

  // Current collaboration for client.
  CollabCase *collab_ = nullptr;

  // Collaboration user id.
  string userid_;
};

void CollabService::Process(HTTPRequest *request, HTTPResponse *response) {
  CollabClient *client = new CollabClient(this, request->conn());
  if (!WebSocket::Upgrade(client, request, response)) {
    delete client;
    response->SendError(404);
    return;
  }
}

void CollabCase::Broadcast(CollabClient *source, const Slice &packet) {
  for (CollabClient *client : clients_) {
    if (client != source) {
      client->Send(packet);
    }
  }
}

void CollabCase::SendKeepAlivePings() {
  time_t now = time(nullptr);
  for (CollabClient *client : clients_) {
    if (now - client->last() > FLAGS_ping) {
      client->Ping("keep-alive", 10);
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

