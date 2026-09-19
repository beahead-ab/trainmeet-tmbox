"""Integration guards for the thin Arduino adapter around the host-tested scope gate."""
from pathlib import Path
import unittest

FIRMWARE = (Path(__file__).parents[1] / "firmware/esp32/TrainMeetTMBox.ino").read_text()


class ESP32MeetScopeTests(unittest.TestCase):
    def section(self, name, next_name):
        return FIRMWARE.split(f"void {name}(", 2)[-1].split(f"void {next_name}(", 1)[0]

    def test_all_three_network_topics_are_checked(self):
        for part in ("Assignment", "Config", "Snapshot"):
            self.assertIn(f"acceptScope(document, tmbox::ScopePart::{part}", FIRMWARE)

    def test_command_uses_the_rendered_scope_without_reloading_it(self):
        body = self.section("sendCommand", "onMqttMessage")
        self.assertIn("!meetScope.ready()", body)
        self.assertIn('document["meet_generation"] = meetScope.value().generation', body)
        self.assertIn('document["publication_id"] = meetScope.value().publication.c_str()', body)
        self.assertIn("if (meetScope.value().present)", body)
        self.assertNotIn("handleSnapshot", body)

    def test_scope_change_discards_old_selection_and_in_flight_state(self):
        body = self.section("invalidateStationCache", "showStationWhenReady")
        for statement in ('pendingAction = ""', 'pendingMessageId = ""', "ackMessageUntil = 0",
                          "navigation = tmbox::LocalNavigationState()", "hasConfig = false", "hasSnapshot = false"):
            self.assertIn(statement, body)
        self.assertIn("if (meetScope.reset_seen())", body)

    def test_delayed_acknowledgements_cannot_restore_old_navigation(self):
        body = self.section("handleAck", "processSavedParameters")
        self.assertIn('pendingMessageId != (document["message_id"] | "")', body)
        self.assertIn('== "stale_meet_context"', body)
        self.assertIn("invalidateStationCache()", body)
        self.assertIn("scopeRefreshRequested = true", body)


if __name__ == "__main__":
    unittest.main()
