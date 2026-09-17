// Writes what the attention controller does with a run of snapshots, so the
// simulator's mirror can be held to the same silences. Silence is the point:
// most of these steps are expected to produce nothing at all.

#include <iostream>

#include "attention.h"
#include "fixtures.h"

using namespace tmbox;

namespace {

const char* CDA = "st-cda";
const char* VST = "st-vst";

const char* attention_name(Attention kind) {
  switch (kind) {
    case Attention::None: return "None";
    case Attention::ConnectionLost: return "ConnectionLost";
    case Attention::IncomingRequest: return "IncomingRequest";
    case Attention::RequestDenied: return "RequestDenied";
    case Attention::RequestApproved: return "RequestApproved";
    case Attention::IncomingTrain: return "IncomingTrain";
    case Attention::ConnectionRestored: return "ConnectionRestored";
  }
  return "?";
}

Snapshot at_cda() {
  Snapshot snapshot;
  snapshot.station_id = CDA;
  snapshot.clock = {"09:00", true};
  return snapshot;
}

Clearance clearance(const std::string& id, const std::string& status, const std::string& from,
                    const std::string& to) {
  Clearance row;
  row.clearance_id = id;
  row.movement_id = "movement-421-cda";
  row.connection_id = "connection-cda-vst";
  row.status = status;
  row.from_station_id = from;
  row.to_station_id = to;
  return row;
}

LineMessage message(const std::string& id, const std::string& from) {
  LineMessage row;
  row.message_id = id;
  row.connection_id = "connection-cda-vst";
  row.status = "delivered_to_device";
  row.from_station_id = from;
  return row;
}

void report(const char* label, const std::vector<AttentionEvent>& events) {
  std::cout << label << " -> ";
  if (events.empty()) {
    std::cout << "(tyst)";
  } else {
    for (std::size_t i = 0; i < events.size(); ++i) {
      if (i) std::cout << " ";
      std::cout << attention_name(events[i].kind);
      if (!events[i].subject.empty()) std::cout << ":" << events[i].subject;
    }
    std::cout << " | loudest=" << attention_name(AttentionController::loudest(events));
  }
  std::cout << "\n";
}

void a_shift_from_boot_to_answer() {
  std::cout << "\n[en-arbetspass-fran-start-till-svar]\n";
  AttentionController attention;

  report("link:up", attention.observe_link(true));

  Snapshot snapshot = at_cda();
  snapshot.clearances = {clearance("clr-1", "waiting", VST, CDA)};
  report("boot-snapshot-med-vantande-begaran", attention.observe(snapshot));

  report("samma-igen", attention.observe(snapshot));

  snapshot.clearances.push_back(clearance("clr-2", "waiting", VST, CDA));
  report("ny-begaran-hit", attention.observe(snapshot));

  snapshot.clearances[0].status = "approved";
  report("vi-godkanner-clr-1-sjalva", attention.observe(snapshot));

  snapshot.clearances.push_back(clearance("clr-3", "waiting", CDA, VST));
  report("var-egen-begaran-ut", attention.observe(snapshot));

  snapshot.clearances[2].status = "rejected";
  report("den-kom-tillbaka-nekad", attention.observe(snapshot));

  snapshot.clearances.push_back(clearance("clr-4", "waiting", CDA, VST));
  report("annu-en-egen-begaran-ut", attention.observe(snapshot));

  snapshot.clearances[3].status = "approved";
  report("den-kom-tillbaka-godkand", attention.observe(snapshot));

  // Taking something away needs no sound: nothing is waiting for the
  // dispatcher to look at.
  snapshot.clearances.push_back(clearance("clr-5", "waiting", VST, CDA));
  report("begaran-hit-som-strax-aterkallas", attention.observe(snapshot));
  snapshot.clearances[4].status = "cancelled";
  report("den-aterkallades", attention.observe(snapshot));

  snapshot.clearances.push_back(clearance("clr-6", "waiting", CDA, VST));
  report("egen-begaran-som-strax-gar-ut", attention.observe(snapshot));
  snapshot.clearances[5].status = "expired";
  report("den-gick-ut", attention.observe(snapshot));

  snapshot.line_messages = {message("msg-1", VST)};
  report("linjen-ledig-mot-oss", attention.observe(snapshot));

  snapshot.line_messages.push_back(message("msg-2", CDA));
  report("vart-eget-meddelande-ekar", attention.observe(snapshot));
}

void the_server_drops_out_mid_shift() {
  std::cout << "\n[servern-forsvinner-mitt-i-passet]\n";
  AttentionController attention;
  report("link:up", attention.observe_link(true));

  Snapshot snapshot = at_cda();
  report("boot-snapshot", attention.observe(snapshot));

  report("link:down", attention.observe_link(false));
  report("link:down-igen", attention.observe_link(false));
  report("link:up", attention.observe_link(true));

  // Everything that happened while the box was blind arrives at once. It is
  // still news - the dispatcher could not have seen any of it.
  snapshot.clearances = {clearance("clr-9", "waiting", VST, CDA)};
  snapshot.line_messages = {message("msg-9", VST)};
  report("allt-som-hant-under-tiden", attention.observe(snapshot));
}

void reassignment_starts_over() {
  std::cout << "\n[omtilldelning-borjar-om]\n";
  AttentionController attention;
  attention.observe_link(true);

  Snapshot cda = at_cda();
  cda.clearances = {clearance("clr-1", "waiting", VST, CDA)};
  report("cda-boot", attention.observe(cda));

  attention.forget();

  Snapshot vst = at_cda();
  vst.station_id = VST;
  vst.clearances = {clearance("clr-5", "waiting", CDA, VST)};
  report("vst-boot-efter-forget", attention.observe(vst));
  report("link:up-igen-efter-forget", attention.observe_link(true));

  vst.clearances.push_back(clearance("clr-6", "waiting", CDA, VST));
  report("ny-begaran-till-vst", attention.observe(vst));
}

}  // namespace

int main() {
  std::cout << "# Genererad av dump_attention.cpp - redigera inte for hand.\n";
  a_shift_from_boot_to_answer();
  the_server_drops_out_mid_shift();
  reassignment_starts_over();
  return 0;
}
