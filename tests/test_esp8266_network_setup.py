"""Keep both firmware families on the server's mDNS contract."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "firmware/esp8266/TrainMeetTambox8266"


class NetworkSetupTest(unittest.TestCase):
    def test_same_discovery_service_as_esp32(self):
        esp32 = (ROOT / "firmware/esp32/TrainMeetTMBox.ino").read_text()
        esp8266 = (SKETCH / "network_setup.h").read_text()
        for source in (esp32, esp8266):
            self.assertRegex(source, r'DISCOVERY_SERVICE\[\]\s*=\s*"tmbox"')

    def test_automatic_server_uses_tested_query_without_manual_override(self):
        source = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        self.assertEqual(source.count("TrainMeetNetwork::queryServers(MDNS)"), 1)
        self.assertNotIn("MDNS.queryService(", source)
        self.assertNotIn("settings.host", source)
        self.assertNotIn("WiFiManagerParameter", source)
        self.assertIn("TrainMeetNetwork::selectServer(MDNS, count, gatewayHost)", source)
        # Service-name repair must never rename the existing wire protocol.
        self.assertIn('"tambox/v1/device/"', source)
        self.assertNotIn("tmbox/v1/", source)
        self.assertNotIn("wifiManager.stopConfigPortal()", source)
        self.assertIn("TrainMeetNetwork::finishSavedPortal(", source)

    def test_discovery_documentation_matches_server(self):
        readme = (ROOT / "firmware/esp8266/README.md").read_text()
        self.assertIn("_tmbox._tcp", readme)
        self.assertNotIn("_tambox._tcp", readme)

    def test_assignment_is_not_a_periodic_heartbeat(self):
        source = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        self.assertNotIn("lastHello", source)
        self.assertIn("serverSync.next(current, refreshRequested)", source)
        self.assertIn("if (request == ServerSync::Assignment) hello()", source)
        self.assertIn("else if (request == ServerSync::State) requestState()", source)
        request = source.split("void requestState()", 1)[1].split("void receiveMessage", 1)[0]
        self.assertIn('"/presence"', request)
        self.assertNotIn("/hello", request)
        self.assertIn('message["state_token"] = stateToken', request)
        self.assertLess(request.index("serverSync.sent("), request.index("publish("))
        hello = source.split("void hello()", 1)[1].split("void requestState()", 1)[0]
        self.assertLess(hello.index("serverSync.sent("), hello.index("publish("))
        self.assertIn('"/state", 1)', source)
        self.assertIn('stateRequestId != (message["request_id"] | "")', source)
        self.assertIn('stateToken == (message["state_token"] | "")', source)
        self.assertIn("lease.heartbeat(millis())", source)
        self.assertIn("serverSync.assignmentReceived(millis())", source)
        self.assertIn("serverSync.reset(); invalidate()", source)
        # ESP32 already requests registration only when MQTT reconnects.
        esp32 = (ROOT / "firmware/esp32/TrainMeetTMBox.ino").read_text()
        self.assertEqual(2, esp32.count("publishHello();")) # declaration + connect

    def test_web_keys_share_server_command_safety(self):
        backend = (SKETCH / "web_test.h").read_text()
        firmware = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        self.assertIn("sendKey(key[0], true)", backend)
        self.assertIn('data["revision"].as<long>() != revision', backend)
        self.assertIn('sessionId != data["session"].as<String>()', backend)
        self.assertIn("webSession.permits(virtualKey, keypadOK && lcdFound", firmware)
        self.assertIn("lease.allowed(millis())", firmware)
        self.assertIn("allowedKeys.indexOf(key) < 0", firmware)
        self.assertIn("sendKey(event.pressed, false)", firmware)
        self.assertIn("SameSite=Strict", backend)
        self.assertIn('origin != "http://" + host', backend)
        self.assertNotIn('data["pin"] =', backend)

    def test_web_javascript_parses_and_has_all_keys(self):
        page = (SKETCH / "web_test_page.h").read_text()
        self.assertIn("123A456B789C*0#D", page)
        self.assertIn("state.allowedKeys.includes", page)
        self.assertNotIn("https://", page)  # Works offline; no CDN.
        node = shutil.which("node")
        if not node:
            self.skipTest("node missing")
        script = page.split("<script>", 1)[1].split("</script>", 1)[0]
        subprocess.run([node, "--check"], input=script, text=True,
                       capture_output=True, check=True, timeout=30)

    def test_digits_are_buffered_before_mqtt_publish(self):
        source = (SKETCH / "TrainMeetTambox8266.ino").read_text()
        send = source.split("bool sendKey(char key, bool virtualKey)", 1)[1].split("#ifndef", 1)[0]
        local = send.index("trainEntry.digit(key)")
        self.assertLess(local, send.index('command["action"]'))
        self.assertIn("return true;", send[local:send.index('command["action"]')])
        self.assertIn('command["train_number"] = trainEntry.value.c_str()', send)
        self.assertIn('message["interaction"]["local_train_entry"] == true', source)

    def test_native_network_and_input_regressions(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ missing")
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / "esp8266-test")
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/esp8266_input_test.cpp"), "-o", binary],
                           check=True, capture_output=True, text=True, timeout=60)
            subprocess.run([binary], check=True, capture_output=True, text=True, timeout=30)
