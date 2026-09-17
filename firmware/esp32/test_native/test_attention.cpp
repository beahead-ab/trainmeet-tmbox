// What earns a sound. Every test here is a case where a box that pipped would
// be wrong — a dispatcher who learns to ignore the buzzer has lost it.

#include "attention.h"
#include "check.h"
#include "fixtures.h"

using namespace tmbox;

namespace {

const char* CDA = "st-cda";
const char* VST = "st-vst";

Snapshot empty_at_cda() {
  Snapshot snapshot;
  snapshot.station_id = CDA;
  snapshot.clock = {"09:00", true};
  return snapshot;
}

Clearance waiting(const std::string& id, const std::string& from, const std::string& to) {
  Clearance clearance;
  clearance.clearance_id = id;
  clearance.movement_id = "movement-421-cda";
  clearance.connection_id = "connection-cda-vst";
  clearance.status = "waiting";
  clearance.from_station_id = from;
  clearance.to_station_id = to;
  return clearance;
}

LineMessage line(const std::string& id, const std::string& from) {
  LineMessage message;
  message.message_id = id;
  message.connection_id = "connection-cda-vst";
  message.status = "sent";
  message.from_station_id = from;
  return message;
}

// A box that has already caught up, so the next snapshot is real news.
AttentionController seeded() {
  AttentionController attention;
  attention.observe(empty_at_cda());
  return attention;
}

// ------------------------------------------------------- rule 2: cold start

void the_first_snapshot_is_never_news() {
  AttentionController attention;
  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-1", VST, CDA), waiting("c-2", VST, CDA)};
  snapshot.line_messages = {line("m-1", VST)};

  check::truthy(attention.observe(snapshot).empty(),
                "boxen ska inte pipa for det som redan pagick nar den startade");
}

void reassignment_makes_the_next_station_a_cold_start_too() {
  AttentionController attention = seeded();
  attention.forget();

  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-9", VST, CDA)};
  check::truthy(attention.observe(snapshot).empty(),
                "efter omtilldelning ar den nya stationens lage inte nyheter");
}

// -------------------------------------------------- rule 1: only a change

void an_incoming_request_rings_once_and_then_stays_quiet() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-1", VST, CDA)};

  std::vector<AttentionEvent> first = attention.observe(snapshot);
  check::truthy(first.size() == 1, "en inkommande begaran ska ge en handelse");
  check::truthy(first[0].kind == Attention::IncomingRequest, "och vara en begaran");
  check::equal("c-1", first[0].subject, "med klareringens id");

  check::truthy(attention.observe(snapshot).empty(),
                "samma begaran ska inte pipa igen sa lange den vantar");
}

void our_own_request_in_flight_is_not_an_incoming_request() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-1", CDA, VST)};  // vi fragar dem

  check::truthy(attention.observe(snapshot).empty(),
                "var egen begaran pa vag ut ar inte nagot att titta upp for");
}

void an_answer_to_our_request_rings() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-1", CDA, VST)};
  attention.observe(snapshot);

  snapshot.clearances[0].status = "approved";
  std::vector<AttentionEvent> events = attention.observe(snapshot);
  check::truthy(events.size() == 1 && events[0].kind == Attention::RequestApproved,
                "godkant svar pa var begaran ska pipa");

  snapshot.clearances[0].status = "rejected";
  events = attention.observe(snapshot);
  check::truthy(events.size() == 1 && events[0].kind == Attention::RequestDenied,
                "och ett nekat svar likasa");
}

void a_cancellation_takes_something_away_and_needs_no_sound() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-1", CDA, VST)};
  attention.observe(snapshot);

  snapshot.clearances[0].status = "cancelled";
  check::truthy(attention.observe(snapshot).empty(),
                "en aterkallad begaran ar inget att titta upp for");

  snapshot.clearances[0].status = "expired";
  check::truthy(attention.observe(snapshot).empty(), "och inte en utgangen heller");
}

void someone_elses_answer_passing_through_is_not_ours() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-1", VST, "st-kun")};
  attention.observe(snapshot);

  snapshot.clearances[0].status = "approved";
  check::truthy(attention.observe(snapshot).empty(),
                "svar pa nagon annans begaran ar inte var nyhet");
}

// ------------------------------------------ rule 3: not news to who caused it
//
// No suppression flag carries this rule. The server only lets the receiver
// answer a clearance and only the sender cancel it, so the direction of a row
// already says who moved it. These two tests are what makes that claim true.

void the_answer_we_just_gave_does_not_ring_at_us() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.clearances = {waiting("c-1", VST, CDA)};  // de fragar oss
  attention.observe(snapshot);

  snapshot.clearances[0].status = "approved";  // vi tryckte A
  check::truthy(attention.observe(snapshot).empty(),
                "svaret man sjalv nyss gav ar man redan och tittar pa");
}

void the_line_we_just_declared_clear_does_not_ring_at_us() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.line_messages = {line("m-1", CDA)};  // vi skickade det
  check::truthy(attention.observe(snapshot).empty(),
                "vart eget linjen-ledig ar inte nagot att titta upp for");
}

// -------------------------------------------------------- line messages

void a_line_declared_clear_towards_us_rings_once() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.line_messages = {line("m-1", VST)};

  std::vector<AttentionEvent> events = attention.observe(snapshot);
  check::truthy(events.size() == 1 && events[0].kind == Attention::IncomingTrain,
                "linjen-ledig mot oss ska pipa");
  check::truthy(attention.observe(snapshot).empty(), "men bara en gang");
}

void our_own_line_message_echoed_back_is_silent() {
  AttentionController attention = seeded();
  Snapshot snapshot = empty_at_cda();
  snapshot.line_messages = {line("m-1", CDA)};
  check::truthy(attention.observe(snapshot).empty(),
                "vart eget meddelande som kommer tillbaka ar inte nyheter");
}

// --------------------------------------------------------------- the link

void losing_the_server_rings_but_only_if_we_had_it() {
  AttentionController attention;
  check::truthy(attention.observe_link(false).empty(),
                "en box som aldrig varit uppkopplad har inte tappat nagot");

  check::truthy(attention.observe_link(true).empty(),
                "forsta uppkopplingen ar inte en aterstalld forbindelse");
  std::vector<AttentionEvent> lost = attention.observe_link(false);
  check::truthy(lost.size() == 1 && lost[0].kind == Attention::ConnectionLost,
                "men att tappa servern ar det");

  std::vector<AttentionEvent> back = attention.observe_link(true);
  check::truthy(back.size() == 1 && back[0].kind == Attention::ConnectionRestored,
                "och att fa tillbaka den");
  check::truthy(attention.observe_link(true).empty(), "att fortsatt vara uppkopplad ar inget");
}

void reassignment_does_not_pretend_the_link_dropped() {
  AttentionController attention;
  attention.observe_link(true);
  attention.forget();
  check::truthy(attention.observe_link(true).empty(),
                "omtilldelning kopplar inte ner, sa den ska inte annonsera en aterkomst");
}

// ------------------------------------------------------------- one buzzer

void the_loudest_event_is_the_one_worth_playing() {
  std::vector<AttentionEvent> events;
  check::truthy(AttentionController::loudest(events) == Attention::None,
                "inget att spela nar inget hant");

  events.push_back(AttentionEvent(Attention::IncomingTrain, "m-1"));
  events.push_back(AttentionEvent(Attention::IncomingRequest, "c-1"));
  check::truthy(AttentionController::loudest(events) == Attention::IncomingRequest,
                "en begaran gar fore linjen-ledig");

  events.push_back(AttentionEvent(Attention::ConnectionLost, ""));
  check::truthy(AttentionController::loudest(events) == Attention::ConnectionLost,
                "men en tappad server gar fore allt - resten gar inte att lita pa");
}

}  // namespace

int main() {
  the_first_snapshot_is_never_news();
  reassignment_makes_the_next_station_a_cold_start_too();
  an_incoming_request_rings_once_and_then_stays_quiet();
  our_own_request_in_flight_is_not_an_incoming_request();
  an_answer_to_our_request_rings();
  a_cancellation_takes_something_away_and_needs_no_sound();
  someone_elses_answer_passing_through_is_not_ours();
  the_answer_we_just_gave_does_not_ring_at_us();
  the_line_we_just_declared_clear_does_not_ring_at_us();
  a_line_declared_clear_towards_us_rings_once();
  our_own_line_message_echoed_back_is_silent();
  losing_the_server_rings_but_only_if_we_had_it();
  reassignment_does_not_pretend_the_link_dropped();
  the_loudest_event_is_the_one_worth_playing();
  return check::report();
}
