#pragma once

#include "model.h"

namespace fixtures {

/// Charlottendal as the spec's reference station: 421 leaves for Vagnsta,
/// 428 arrives from it.
inline tmbox::StationConfig charlottendal() {
  tmbox::StationConfig config;
  config.station_id = "st-cda";
  config.code = "CDA";
  config.name = "Charlottendal";
  config.tracks = {{"track-cda-1a", "1A"}, {"track-cda-1b", "1B"},
                   {"track-cda-2a", "2A"}, {"track-cda-2b", "2B"}};
  config.connections = {{"connection-cda-vst", "VST", "single"},
                        {"connection-cda-kun", "KUN", "double"}};
  return config;
}

inline tmbox::Snapshot two_movements() {
  tmbox::Snapshot snapshot;
  snapshot.station_id = "st-cda";
  snapshot.clock = {"09:00", true};

  tmbox::Movement departure;
  departure.id = "movement-421-cda";
  departure.train_number = "421";
  departure.departure_time = "09:20";
  departure.departure = "none";
  departure.arrival = "none";
  departure.assigned_track_id = "track-cda-1b";
  departure.allowed_actions = {"train.position.set"};

  tmbox::Movement arrival;
  arrival.id = "movement-428-cda";
  arrival.train_number = "428";
  arrival.arrival_time = "09:41";
  arrival.departure = "none";
  arrival.arrival = "none";
  arrival.assigned_track_id = "track-cda-2a";
  arrival.allowed_actions = {"train.approaching", "train.arrived"};

  snapshot.movements = {departure, arrival};
  return snapshot;
}

}  // namespace fixtures
