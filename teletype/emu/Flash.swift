// The Flash window: puts the Orca Seaflake firmware (or monome's stock
// Teletype firmware) onto a Teletype that sits in its bootloader on USB.
//
// Before flashing it checks the ports: the 32-byte configuration block the
// firmware carries at 0x80044C00 (src/seaflake/sf_config.h) says which
// Disting EX unit and which ER-301 the Orca operators send to. The table
// compares it with what the module last saw on its I2C bus (its flash head,
// read back over DFU) or with SFSCAN.TXT on a USB stick, and the choices are
// written into the .hex before dfu-programmer flashes it.
//
// The three commands are monome's own: erase, flash --suppress-bootloader-mem,
// start. Their output is shown live. Resources/flashing.txt holds the steps
// (the same text as the manual and the README).
import SwiftUI
import AppKit
import IOKit
import IOKit.usb
import CryptoKit
import UniformTypeIdentifiers

let dfuVendor = 0x03eb, dfuProduct = 0x2ff6   // the AT32UC3B0512 bootloader: Atmel DFU
let chip = "at32uc3b0512"
let releasesURL = URL(string: "https://github.com/monome/teletype/releases")!
let configAddr: UInt32 = 0x80044C00           // sf_config_t
let headAddr: UInt32 = 0x80044E00             // sf_head_t in .flash_sf (module/sf_flash.c)

enum Firmware: String, CaseIterable, Identifiable { case orca = "ORCA SEAFLAKE", stock = "STOCK TELETYPE"; var id: String { rawValue } }

// the ports configuration block
struct Ports: Equatable {
    var version = 3     // 1: firmware from before the startup preset (2026-11-15), 2: before the EX address (2026-11-19)
    var exAddr = 65     // the Disting EX's I2C address (unit 1), as its own Settings menu shows it: 65 = 0x41
    var exUnit = 0      // 0 = auto (first found), 1-4 (= exAddr + unit - 1)
    var erUnit = 0      // 0 = auto, 1-3
    var pulse = 20
    var midiViaEx = true
    var bootPresetOn = false   // version 2: load a Disting EX preset once at power-on
    var bootPreset = 0
    var exAddrOK: Bool { (1...124).contains(exAddr) }
    func hex(_ a: Int) -> String { String(format: "0x%02X", a) }
    var text: String {
        "DISTING EX 0x41, ER-301 0x31 (FIXED), PULSE \(pulse) MS"
            + (version >= 2 ? ", STARTUP PRESET \(bootPresetOn ? String(bootPreset) : "OFF")" : "")
    }
}

// what an I2C scan found: from the module's flash head or SFSCAN.TXT
struct Scan {
    var ex = [Bool](repeating: false, count: 4), er = [Bool](repeating: false, count: 3)
    var exUnit = 0, erUnit = 0, alg = [Int](repeating: 0, count: 4)
    var source = ""
    var text: String {
        var s: [String] = []
        for u in 0..<4 where ex[u] { s.append("EX\(u + 1) ALG \(alg[u])") }
        for n in 0..<3 where er[n] { s.append("ER-301 \(n + 1) (0x3\(n + 1))") }
        return s.isEmpty ? "nothing answered on the I2C bus" : s.joined(separator: ", ")
    }
}

// Intel HEX in and out (addresses as in the file, 0x8000xxxx for the Teletype)
struct Hex {
    var mem: [UInt32: UInt8] = [:]
    init?(path: String) {
        guard let d = FileManager.default.contents(atPath: path) else { return nil }
        self.init(text: String(decoding: d, as: UTF8.self))
    }
    init(text: String) {
        var base: UInt32 = 0
        for l in text.split(whereSeparator: { $0.isNewline }) where l.hasPrefix(":") && l.count >= 11 {
            let h = Array(l.dropFirst())
            var b: [UInt8] = []
            var i = 0
            while i + 1 < h.count, let v = UInt8(String(h[i...i + 1]), radix: 16) { b.append(v); i += 2 }
            guard b.count >= 5 else { continue }
            let n = Int(b[0]), a = UInt32(b[1]) << 8 | UInt32(b[2]), t = b[3]
            if t == 4 && b.count >= 6 { base = (UInt32(b[4]) << 8 | UInt32(b[5])) << 16 }
            else if t == 2 && b.count >= 6 { base = (UInt32(b[4]) << 8 | UInt32(b[5])) << 4 }
            else if t == 0 { for k in 0..<min(n, b.count - 5) { mem[base + a + UInt32(k)] = b[4 + k] } }
        }
    }
    var bytes: Int { mem.count }
    // the byte at addr, trying the file's 0x8000xxxx form and the bootloader's 0-based form
    func at(_ addr: UInt32) -> UInt8? { mem[addr] ?? mem[addr & 0x7FFFF] }
    func block(_ addr: UInt32, _ n: Int) -> [UInt8]? {
        var out: [UInt8] = []
        for i in 0..<n { guard let b = at(addr + UInt32(i)) else { return nil }; out.append(b) }
        return out
    }
    // the block parsed by the firmware's own code (src/seaflake/sf_config.c, through bridge.h)
    func ports() -> Ports? {
        guard let b = block(configAddr, 32) else { return nil }
        var c = sf_ports_t()
        guard sf_config_parse(b, &c) else { return nil }
        return Ports(version: Int(c.version), exAddr: Int(c.ex_addr), exUnit: Int(c.ex_unit), erUnit: Int(c.er_unit), pulse: Int(c.pulse_ms),
                     midiViaEx: c.midi_via_ex != 0, bootPresetOn: c.boot_preset_on != 0, bootPreset: Int(c.boot_preset))
    }
    func lastScan() -> Scan? {
        guard let b = block(headAddr, 15), b[0] == 0x5E, b[1] == 0xAF, b[2] == 0x1A, b[3] == 0x01, b[6] == 0xA5 else { return nil }
        var s = Scan()
        for u in 0..<4 { s.ex[u] = b[7] & (1 << u) != 0; s.alg[u] = Int(b[11 + u]) }
        for n in 0..<3 { s.er[n] = b[8] & (1 << n) != 0 }
        s.exUnit = Int(b[9]); s.erUnit = Int(b[10]); s.source = "the module's last I2C scan (read over USB)"
        return s
    }
    // write the ports into the block as a version 3 block with its checksum
    // (sf_config_build: the firmware's own code). Only for a .hex that
    // already carries a version 3 block (this app's own firmware).
    mutating func set(_ p: Ports) {
        let base = mem[configAddr] != nil ? configAddr : configAddr & 0x7FFFF
        var c = sf_ports_t()
        sf_config_defaults(&c)
        c.ex_addr = 65; c.ex_unit = 0; c.er_unit = 0  // fixed addresses: 0x41 / 0x31
        c.pulse_ms = UInt8(clamping: p.pulse); c.midi_via_ex = p.midiViaEx ? 1 : 0
        c.boot_preset_on = p.bootPresetOn ? 1 : 0; c.boot_preset = UInt16(clamping: p.bootPreset)
        var b = [UInt8](repeating: 0, count: 32)
        sf_config_build(&b, &c)
        for i in 0..<32 { mem[base + UInt32(i)] = b[i] }
    }
    // the 32 bytes of the config block (after set), for the emulator
    func configBlock() -> [UInt8]? { block(configAddr, 32) }
    func write(to path: String) throws {
        var out = "", base: UInt32 = 0xFFFFFFFF
        let keys = mem.keys.sorted()
        var i = 0
        func rec(_ t: UInt8, _ a: UInt16, _ d: [UInt8]) {
            var b: [UInt8] = [UInt8(d.count), UInt8(a >> 8), UInt8(a & 0xFF), t] + d
            b.append(UInt8(truncatingIfNeeded: 0 &- b.map(Int.init).reduce(0, +)))
            out += ":" + b.map { String(format: "%02X", $0) }.joined() + "\r\n"
        }
        while i < keys.count {
            let a = keys[i]
            if a >> 16 != base { base = a >> 16; rec(4, 0, [UInt8(base >> 8), UInt8(base & 0xFF)]) }
            var d: [UInt8] = []
            while i < keys.count, keys[i] == a + UInt32(d.count), d.count < 16, keys[i] >> 16 == base { d.append(mem[keys[i]]!); i += 1 }
            rec(0, UInt16(a & 0xFFFF), d)
        }
        rec(1, 0, [])
        try out.write(toFile: path, atomically: true, encoding: .utf8)
    }
}

func parseScanText(_ t: String, source: String) -> Scan? {
    guard t.contains("ORCA SEAFLAKE I2C SCAN") else { return nil }
    var s = Scan(); s.source = source
    for l in t.split(whereSeparator: { $0.isNewline }) {
        let w = l.split(separator: " ").map(String.init)
        if w.count >= 2, w[0].hasPrefix("EX"), let u = Int(w[0].dropFirst(2)), (1...4).contains(u) {
            s.ex[u - 1] = w[1] == "FOUND"
            if let i = w.firstIndex(of: "ALG"), i + 1 < w.count { s.alg[u - 1] = Int(w[i + 1]) ?? 0 }
        }
        if w.count >= 3, w[0] == "ER301", let n = Int(w[1]), (1...3).contains(n) { s.er[n - 1] = w[2] == "FOUND" }
        if w.count >= 5, w[0] == "USING" { s.exUnit = Int(w[2]) ?? 0; s.erUnit = Int(w[4]) ?? 0 }
    }
    return s
}

enum Status { case ok(String), wrong(String), unverified(String)
    var text: String { switch self { case .ok(let s): return "OK  " + s; case .wrong(let s): return "WRONG  " + s; case .unverified(let s): return "UNVERIFIED  " + s } }
    var color: Color { switch self { case .ok: return .cyan; case .wrong: return .pink; case .unverified: return .amber } }
    var isWrong: Bool { if case .wrong = self { return true }; return false }
}

final class FlashModel: ObservableObject {
    static let shared = FlashModel()
    @Published var tool: String? = nil
    @Published var brew: String? = nil
    @Published var dfuPresent = false
    @Published var bootloader = ""
    @Published var firmware: Firmware = .orca
    @Published var orcaHex: Hex? = nil
    @Published var orcaPath: String? = nil
    @Published var orcaInfo = ""
    @Published var stockHex: String? = nil
    @Published var stockInfo = "no file chosen yet"
    @Published var log: [(String, Color)] = []
    @Published var busy = false
    @Published var step = 0.0
    @Published var result: (String, Bool)? = nil
    @Published var showInstructions = true
    @Published var instructions: [String] = []
    // the ports check
    @Published var hexPorts: Ports? = nil         // the defaults compiled into the .hex
    // what will be flashed; the emulator runs with the same block, so it
    // behaves as the module will (startup preset, pulse, pinned units)
    @Published var ports = Ports() { didSet { if ports != oldValue { toEmulator() } } }
    func toEmulator() {
        guard var h = orcaHex, hexPorts != nil else { return }
        h.set(ports)
        if let b = h.configBlock() { Emu.shared.setConfig(b) }
    }
    @Published var devicePorts: Ports? = nil      // read from the module
    @Published var deviceNote = "module not read yet"
    @Published var scan: Scan? = nil              // what the I2C bus looked like, if known
    @Published var override = false
    private var timer: Timer?
    private var readDone = false

    private init() {
        findTool()
        for p in [Bundle.main.resourcePath.map { $0 + "/orca-seaflake-teletype.hex" }, "../dist/orca-seaflake-teletype.hex"].compactMap({ $0 })
            where FileManager.default.isReadableFile(atPath: p) { orcaPath = p; orcaHex = Hex(path: p); orcaInfo = describe(p); break }
        if orcaHex == nil { orcaInfo = "orca-seaflake-teletype.hex is missing from the app" }
        hexPorts = orcaHex?.ports()
        if let h = hexPorts { ports = h } else { orcaInfo += "  (no ports block found in it)" }
        if let p = resource("flashing.txt"), let t = try? String(contentsOfFile: p, encoding: .utf8) { instructions = t.components(separatedBy: "\n") }
        say("PUT THE TELETYPE IN ITS BOOTLOADER (STEP 1). IT IS LOOKED FOR EVERY 2 SECONDS.", .dimc)
        readStick(quiet: true)
        poll()
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in self?.poll() }
    }

    func say(_ s: String, _ c: Color = .txt) { log.append((s, c)); if log.count > 600 { log.removeFirst(100) } }

    var statusLine: String {
        if tool == nil { return "FLASHER: INSTALL DFU-PROGRAMMER (CMD-F)" }
        if busy { return "FLASHING... DO NOT UNPLUG" }
        return dfuPresent ? "TELETYPE BOOTLOADER FOUND - CMD-F TO FLASH" : "NO TELETYPE BOOTLOADER ON USB (CMD-F FOR THE STEPS)"
    }
    var statusColor: Color { tool == nil ? .amber : busy ? .amber : dfuPresent ? .cyan : .dimc }

    func findTool() {
        tool = ["/opt/homebrew/bin/dfu-programmer", "/usr/local/bin/dfu-programmer", "/opt/local/bin/dfu-programmer"].first { FileManager.default.isExecutableFile(atPath: $0) }
        brew = ["/opt/homebrew/bin/brew", "/usr/local/bin/brew"].first { FileManager.default.isExecutableFile(atPath: $0) }
    }

    func describe(_ path: String) -> String {
        guard let d = FileManager.default.contents(atPath: path) else { return "unreadable" }
        let h = Hex(text: String(decoding: d, as: UTF8.self))
        let sha = SHA256.hash(data: d).map { String(format: "%02x", $0) }.joined()
        return "\((path as NSString).lastPathComponent)  \(h.bytes) bytes of flash (\(h.bytes * 100 / 524288)% of the chip)  sha256 \(sha.prefix(12))…"
    }

    static func usbDFU() -> Bool {
        for cls in ["IOUSBHostDevice", "IOUSBDevice"] {
            guard let m = IOServiceMatching(cls) as NSMutableDictionary? else { continue }
            m["idVendor"] = dfuVendor; m["idProduct"] = dfuProduct
            var it: io_iterator_t = 0
            if IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS { continue }
            let s = IOIteratorNext(it)
            IOObjectRelease(it)
            if s != 0 { IOObjectRelease(s); return true }
        }
        return false
    }

    func poll() {
        if busy { return }
        if tool == nil { findTool() }
        let now = FlashModel.usbDFU()
        if now != dfuPresent {
            dfuPresent = now
            if now {
                say("TELETYPE BOOTLOADER FOUND ON USB (ATMEL DFU 03EB:2FF6).", .cyan)
                if let t = tool {
                    var v = ""
                    run(t, [chip, "get", "bootloader-version"], onLine: { l in if !l.isEmpty { v = l } }) { _ in
                        self.bootloader = v.trimmingCharacters(in: .whitespaces)
                        self.readDevice()
                    }
                }
            } else { bootloader = ""; readDone = false; devicePorts = nil; deviceNote = "module not read yet"; say("TELETYPE GONE FROM USB.", .dimc) }
        }
    }

    func run(_ exe: String, _ args: [String], onLine: @escaping (String) -> Void, done: @escaping (Int32) -> Void) {
        let p = Process(); p.executableURL = URL(fileURLWithPath: exe); p.arguments = args
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:/opt/local/bin:" + (env["PATH"] ?? "/usr/bin:/bin")
        env["HOMEBREW_NO_AUTO_UPDATE"] = "1"
        p.environment = env
        let pipe = Pipe(); p.standardOutput = pipe; p.standardError = pipe
        var buf = Data()
        pipe.fileHandleForReading.readabilityHandler = { h in
            let d = h.availableData; if d.isEmpty { return }
            buf.append(d)
            while let r = buf.range(of: Data([10])) {
                let line = String(decoding: buf.subdata(in: 0..<r.lowerBound), as: UTF8.self)
                buf.removeSubrange(0..<r.upperBound)
                DispatchQueue.main.async { onLine(line.trimmingCharacters(in: .init(charactersIn: "\r"))) }
            }
        }
        p.terminationHandler = { pr in
            DispatchQueue.main.async {
                pipe.fileHandleForReading.readabilityHandler = nil
                if !buf.isEmpty { onLine(String(decoding: buf, as: UTF8.self)) }
                done(pr.terminationStatus)
            }
        }
        do { try p.run() } catch { onLine("could not start \(exe): \(error.localizedDescription)"); done(1) }
    }

    // the ports check, part 1: what is on the module now (its config block and last scan)
    func readDevice() {
        guard let t = tool, dfuPresent, !busy else { return }
        busy = true
        say("READING THE MODULE'S FLASH (dfu-programmer read)...", .amber)
        var text = ""
        run(t, [chip, "read"], onLine: { text += $0 + "\n" }) { code in
            self.busy = false
            self.readDone = true
            let h = Hex(text: text)
            if code != 0 && h.bytes == 0 {
                self.devicePorts = nil
                self.deviceNote = "the module could not be read back (exit \(code)); comparing with the defaults in the .hex"
                self.say("READ FAILED; THE PORTS TABLE SHOWS THE .HEX DEFAULTS.", .pink)
                return
            }
            self.say("READ \(h.bytes) BYTES FROM THE MODULE.", .dimc)
            if let p = h.ports() {
                self.devicePorts = p
                self.deviceNote = "module holds Orca Seaflake: ports \(p.text)"
                self.say("MODULE PORTS: \(p.text)", .cyan)
                if let s = h.lastScan() { self.scan = s; self.say("MODULE'S LAST SCAN: \(s.text)", .cyan) }
                else { self.say("THE MODULE HAS NOT RUN A SCAN YET (NO LAST SCAN STORED).", .dimc) }
            } else {
                self.devicePorts = nil
                self.deviceNote = h.bytes == 0 ? "the module's flash is blank; comparing with the defaults in the .hex"
                                               : "no Orca Seaflake on the module (stock Teletype?); comparing with the defaults in the .hex"
                self.say(self.deviceNote.uppercased(), .dimc)
            }
        }
    }

    // part 2: SFSCAN.TXT on a USB stick in /Volumes, if the module wrote one
    func readStick(quiet: Bool = false) {
        let fm = FileManager.default
        for v in (try? fm.contentsOfDirectory(atPath: "/Volumes")) ?? [] {
            for n in ["SFSCAN.TXT", "sfscan.txt"] {
                let p = "/Volumes/\(v)/\(n)"
                if let t = try? String(contentsOfFile: p, encoding: .utf8), let s = parseScanText(t, source: "SFSCAN.TXT on \(v)") {
                    scan = s; say("SCAN FROM \(p): \(s.text)", .cyan); return
                }
            }
        }
        if !quiet { say("NO SFSCAN.TXT ON ANY VOLUME IN /VOLUMES (WRITE TO USB ON THE MODULE WRITES ONE).", .dimc) }
    }

    // the table
    struct PortRow: Identifiable { let id: Int; let dest, proto, addr, ports: String; let status: Status }
    var rows: [PortRow] {
        // fixed addresses (2026-11-20): the Disting EX at 0x41, the ER-301 at 0x31
        let exAddr = "65 = 0x41 (fixed)", erAddr = "0x31 (fixed)"
        var ex: Status
        if let s = scan { ex = s.ex[0] ? .ok("answered at 0x41") : .unverified("did not answer at the last scan (\(s.source)); set its Settings > I2C address to 65") }
        else { ex = .ok("set the Disting EX's Settings > I2C address to 65") }
        let er: Status = .ok("fixed at 0x31; shows FOUND on the module after the first < or a-z message")
        let pulseOK = (1...250).contains(ports.pulse)
        var preset: Status
        if ports.version < 2 { preset = .ok("this firmware has no startup preset") }
        else if !ports.bootPresetOn { preset = .ok("off: no preset is sent at power-on") }
        else if !(0...32767).contains(ports.bootPreset) { preset = .wrong("preset must be 0-32767") }
        else if let s = scan, !s.ex.contains(true) { preset = .unverified("sent only once a Disting EX answers; none did (\(s.source))") }
        else { preset = .ok("EX.PRESET \(ports.bootPreset) once, when the Disting EX answers (it keeps looking for 2 min)") }
        return [
            PortRow(id: 0, dest: "Disting EX  (> and the MIDI ops)", proto: "I2C, Teletype EX.* ops", addr: exAddr, ports: "N (>n), N@ (>@) and every >x function; no unit or channel cells", status: ex),
            PortRow(id: 1, dest: "Disting EX MIDI out", proto: "I2C EX.M.N# / NO# / CC# / PB, out of the EX's MIDI breakout", addr: "same unit as above",
                    ports: ": % ! ? ( ) @ channel cell 0-f = MIDI channel 1-16", status: ports.midiViaEx ? ex : .wrong("midi_via_ex is off")),
            PortRow(id: 2, dest: "ER-301 Sound Computer  (< and outputs a-z)", proto: "I2C, Teletype SC.TR / SC.CV ops", addr: erAddr,
                    ports: "a-z = ports 1-26; < takes any port", status: er),
            PortRow(id: 3, dest: "Teletype TR / CV  (outputs 1-4)", proto: "the module's own jacks", addr: "-",
                    ports: "TR 1-4 pulses \(ports.pulse) ms ($pulse), CV 1-4 at 1 V/octave (4C = 4 V)", status: pulseOK ? .ok("fixed") : .wrong("pulse must be 1-250 ms")),
            PortRow(id: 4, dest: "Disting EX startup preset  (loadbang)", proto: "I2C EX.PRESET, once at power-on", addr: "-",
                    ports: "optional; a ` in a patch does the same per patch (e.g. ` above >l05)", status: preset),
        ]
    }
    var portsOK: Bool { !rows.contains { $0.status.isWrong } }
    var portsChanged: Bool { hexPorts != nil && ports != hexPorts }

    func install() {
        guard let b = brew else {
            say("HOMEBREW IS NOT INSTALLED. GET IT FROM BREW.SH, THEN: brew install dfu-programmer", .amber)
            NSWorkspace.shared.open(URL(string: "https://brew.sh")!)
            return
        }
        busy = true; result = nil
        say("$ brew install dfu-programmer", .cyan)
        run(b, ["install", "dfu-programmer"], onLine: { self.say("  " + $0, .dimc) }) { code in
            self.busy = false
            self.findTool()
            if let t = self.tool { self.result = ("dfu-programmer installed at \(t)", false); self.say("DFU-PROGRAMMER INSTALLED.", .cyan) }
            else { self.result = ("brew install dfu-programmer failed (exit \(code)); run it in Terminal to see why", true) }
        }
    }

    func chooseStock() {
        let pnl = NSOpenPanel()
        pnl.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
        pnl.allowedContentTypes = [UTType(filenameExtension: "hex") ?? .data, .zip]
        pnl.message = "Choose teletype.hex, or the release .zip from github.com/monome/teletype/releases"
        guard pnl.runModal() == .OK, let u = pnl.url else { return }
        if u.pathExtension.lowercased() == "zip" {
            let dir = NSTemporaryDirectory() + "orca-seaflake-stock-\(Int(Date().timeIntervalSince1970))"
            try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
            say("$ unzip -j \(u.lastPathComponent) '*.hex'", .cyan)
            run("/usr/bin/unzip", ["-o", "-j", u.path, "*.hex", "-d", dir], onLine: { self.say("  " + $0, .dimc) }) { _ in
                let hexes = ((try? FileManager.default.contentsOfDirectory(atPath: dir)) ?? []).filter { $0.hasSuffix(".hex") }
                if let h = hexes.first(where: { $0 == "teletype.hex" }) ?? hexes.first {
                    self.stockHex = dir + "/" + h; self.stockInfo = self.describe(dir + "/" + h); self.firmware = .stock
                } else { self.stockInfo = "no .hex inside \(u.lastPathComponent)"; self.say("NO .HEX IN THAT ZIP.", .pink) }
            }
        } else { stockHex = u.path; stockInfo = describe(u.path); firmware = .stock }
    }

    // the file dfu-programmer gets: the stock hex as chosen, or the Orca hex with the ports written in
    func hexToFlash() -> String? {
        if firmware == .stock { return stockHex }
        guard var h = orcaHex, let path = orcaPath else { return nil }
        if !portsChanged { return path }
        h.set(ports)
        let out = NSTemporaryDirectory() + "orca-seaflake-teletype-ports.hex"
        do { try h.write(to: out) } catch { say("COULD NOT WRITE THE PATCHED HEX: \(error.localizedDescription)", .pink); return nil }
        say("PORTS WRITTEN INTO THE FIRMWARE AT 0x80044C00: \(ports.text) (checksum updated)", .cyan)
        return out
    }
    var canFlash: Bool { !busy && tool != nil && dfuPresent && (firmware == .stock ? stockHex != nil : orcaHex != nil) && (firmware == .stock || portsOK || override) }

    func flash() {
        guard let t = tool, let hex = hexToFlash() else { return }
        busy = true; result = nil; step = 0
        say("FLASHING \(firmware.rawValue): \((hex as NSString).lastPathComponent)", .cyan)
        let steps: [(String, [String])] = [("1/3 ERASE", [chip, "erase"]),
                                           ("2/3 FLASH", [chip, "flash", hex, "--suppress-bootloader-mem"]),
                                           ("3/3 START", [chip, "start"])]
        func go(_ i: Int) {
            if i == steps.count {
                step = 1; busy = false
                result = ("DONE. The Teletype restarts by itself: unplug the USB cable. When it says SCENES WILL BE OVERWRITTEN, press the front-panel button once.", false)
                say("DONE. THE TELETYPE RESTARTS. PRESS ITS FRONT-PANEL BUTTON WHEN IT ASKS.", .cyan)
                return
            }
            let (name, args) = steps[i]
            say(name + "   $ dfu-programmer " + args.joined(separator: " ").replacingOccurrences(of: hex, with: (hex as NSString).lastPathComponent), .amber)
            run(t, args, onLine: { if !$0.isEmpty { self.say("  " + $0, .dimc) } }) { code in
                self.step = Double(i + 1) / 3
                if code != 0 && i < 2 {   // (start reports an error after the chip has already left: fine)
                    self.busy = false
                    self.result = ("\(name) failed (exit \(code)). Nothing is damaged: put the Teletype back in its bootloader and press FLASH again.", true)
                    self.say("FAILED AT \(name). THE BOOTLOADER CAN'T BE ERASED: TRY AGAIN.", .pink)
                } else { go(i + 1) }
            }
        }
        go(0)
    }
}

////////////////////////////////////////////////////////////////////////////

struct LabelRow<Content: View>: View {
    let label: String; let content: Content
    init(_ label: String, @ViewBuilder _ c: () -> Content) { self.label = label; content = c() }
    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Text(label).font(mono(11, .semibold)).foregroundColor(.dimc).frame(width: 130, alignment: .leading)
            content
        }
    }
}

// the hard-wrapped flashing.txt as blocks: a heading, a code line, a bullet/step, or a paragraph
func blocks(_ lines: [String]) -> [String] {
    var out: [String] = []
    for l in lines {
        let code = l.hasPrefix("    "), head = l.hasPrefix("#"), item = l.hasPrefix("- ") || l.range(of: "^[0-9]+\\. ", options: .regularExpression) != nil
        if l.isEmpty || code || head || item || out.isEmpty || out.last!.isEmpty || out.last!.hasPrefix("#") || out.last!.hasPrefix("    ") { out.append(l) }
        else { out[out.count - 1] += " " + l.trimmingCharacters(in: .whitespaces) }
    }
    return out
}

struct Instructions: View {
    let lines: [String]
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 2) {
                ForEach(Array(blocks(lines).enumerated()), id: \.offset) { _, l in
                    if l.hasPrefix("# ") { Text(l.dropFirst(2).uppercased()).font(mono(13, .bold)).foregroundColor(.cyan).padding(.top, 10) }
                    else if l.hasPrefix("## ") { Text(l.dropFirst(3)).font(mono(12, .semibold)).foregroundColor(.amber).padding(.top, 6) }
                    else if l.hasPrefix("    ") { Text(l.dropFirst(4)).font(mono(11)).foregroundColor(.cyan).padding(.leading, 12) }
                    else if l.isEmpty { Text(" ").font(mono(6)) }
                    else { Text(l).font(mono(11)).foregroundColor(.txt).fixedSize(horizontal: false, vertical: true) }
                }
            }.frame(maxWidth: .infinity, alignment: .leading).padding(14)
        }.background(Color.panel).cornerRadius(4)
    }
}

struct PortsTable: View {
    @ObservedObject var m: FlashModel
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .top, spacing: 8) {
                ForEach(Array(["DESTINATION", "PROTOCOL", "ADDRESS", "PORTS / CHANNELS", "STATUS"].enumerated()), id: \.offset) { i, h in
                    Text(h).font(mono(10, .semibold)).foregroundColor(.dimc).frame(width: [140, 140, 150, 180, 170][i], alignment: .leading)
                }
            }
            ForEach(m.rows) { r in
                HStack(alignment: .top, spacing: 8) {
                    Text(r.dest).font(mono(10.5, .semibold)).foregroundColor(.txt).frame(width: 140, alignment: .leading)
                    Text(r.proto).font(mono(10)).foregroundColor(.dimc).frame(width: 140, alignment: .leading)
                    Group {
                        if r.id == 0 { Text("I2C 65 = 0x41 (fixed)").font(mono(10)).foregroundColor(.dimc)
                        } else if r.id == 2 { Text("I2C 0x31 (fixed)").font(mono(10)).foregroundColor(.dimc)
                        } else if r.id == 4 {
                            HStack(spacing: 4) {
                                Toggle(isOn: $m.ports.bootPresetOn) { Text("load").font(mono(10)).foregroundColor(.dimc) }.toggleStyle(.checkbox)
                                    .disabled(m.busy || m.ports.version < 2)
                                TextField("", value: $m.ports.bootPreset, formatter: NumberFormatter()).textFieldStyle(.plain).font(mono(10)).foregroundColor(.txt)
                                    .frame(width: 44).padding(3).background(Color.bgc).cornerRadius(2).disabled(!m.ports.bootPresetOn || m.busy)
                            }
                        } else if r.id == 3 {
                            HStack(spacing: 4) {
                                Text("pulse").font(mono(10)).foregroundColor(.dimc)
                                TextField("", value: $m.ports.pulse, formatter: NumberFormatter()).textFieldStyle(.plain).font(mono(10)).foregroundColor(.txt)
                                    .frame(width: 36).padding(3).background(Color.bgc).cornerRadius(2)
                                Text("ms").font(mono(10)).foregroundColor(.dimc)
                            }
                        } else { Text(r.addr).font(mono(10)).foregroundColor(.dimc) }
                    }.frame(width: 150, alignment: .leading)
                    Text(r.ports).font(mono(10)).foregroundColor(.dimc).frame(width: 180, alignment: .leading)
                    Text(r.status.text).font(mono(10, .semibold)).foregroundColor(r.status.color).frame(width: 170, alignment: .leading)
                }.fixedSize(horizontal: false, vertical: true)
                Rectangle().fill(Color.bgc).frame(height: 1)
            }
            Text("MODULE: " + m.deviceNote).font(mono(10)).foregroundColor(.dimc)
            Text("EXPECTED FROM: " + (m.scan.map { "\($0.source): \($0.text)" } ?? "nothing read yet; the table checks what can be checked without a scan")).font(mono(10)).foregroundColor(.dimc)
            Text("ADDRESSES ARE FIXED: the Disting EX at 65 (0x41): its Settings > I2C address = 65 (its factory 49 is the ER-301's address). The ER-301 at 0x31.")
                .font(mono(10, .semibold)).foregroundColor(.amber).fixedSize(horizontal: false, vertical: true)
            Text("Also on the Disting EX: in the algorithm, I2C notes on (e.g. SD Multisample answers EX.NOTE on channel 1). The MIDI operators leave the Disting's MIDI breakout on the channel in their channel cell.")
                .font(mono(10)).foregroundColor(.dimc).fixedSize(horizontal: false, vertical: true)
            HStack(spacing: 10) {
                Button("READ MODULE AGAIN") { m.readDevice() }.buttonStyle(Key(tint: .dimc)).disabled(m.busy || !m.dfuPresent || m.tool == nil)
                Button("READ USB STICK") { m.readStick() }.buttonStyle(Key(tint: .dimc)).disabled(m.busy)
                if let h = m.hexPorts, m.ports != h { Button("BACK TO DEFAULTS") { m.ports = h }.buttonStyle(Key(tint: .dimc)) }
                Toggle(isOn: $m.override) { Text("flash anyway").font(mono(10)).foregroundColor(.amber) }.toggleStyle(.checkbox)
                Spacer()
                Pill(text: m.portsOK ? "PORTS OK" : "PORTS WRONG - FIX THE ADDRESSES OR TICK FLASH ANYWAY", color: m.portsOK ? .cyan : .pink)
            }
        }.padding(10).background(Color.panel).cornerRadius(4)
    }
}

// The Flash window: three calm steps (connect, set the Disting EX address,
// flash). Everything else (pinned units, pulse, the module read-back, the
// ports table, the file's sha256 and the raw log) is under DETAILS.
struct FlashView: View {
    @ObservedObject var m = FlashModel.shared
    @State var confirm = false
    @State var details = false
    @State var guide = false

    func stepHead(_ n: String, _ t: String) -> some View {
        HStack(spacing: 10) {
            Text(n).font(mono(12, .bold)).foregroundColor(.bgc).frame(width: 22, height: 22).background(Color.cyan).cornerRadius(11)
            Text(t).font(mono(12, .semibold)).foregroundColor(.txt)
        }
    }

    // the first thing that is wrong in the ports check (address range etc.)
    var portsProblem: String? {
        guard m.firmware == .orca, !m.portsOK else { return nil }
        if let r = m.rows.first(where: { $0.status.isWrong }), case .wrong(let s) = r.status { return s }
        return nil
    }

    var body: some View {
        let w: CGFloat = details ? 880 : 560
        let screenH = (NSScreen.main?.visibleFrame.height ?? 800) - 60
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text("FLASH THE TELETYPE").font(mono(16, .bold)).foregroundColor(.cyan)

                // 1. connect
                VStack(alignment: .leading, spacing: 8) {
                    stepHead("1", "CONNECT")
                    if m.tool == nil {
                        Text("dfu-programmer is not installed (needed to flash).").font(mono(11)).foregroundColor(.amber)
                        Button("INSTALL DFU-PROGRAMMER") { m.install() }.buttonStyle(Key(tint: .amber)).disabled(m.busy)
                    } else if m.dfuPresent {
                        Text("● Teletype ready to flash").font(mono(13, .semibold)).foregroundColor(.cyan)
                    } else {
                        Text("○ Teletype not connected: put it in bootloader mode").font(mono(13, .semibold)).foregroundColor(.amber)
                        VStack(alignment: .leading, spacing: 3) {
                            Text("1  power off the case").font(mono(11)).foregroundColor(.txt)
                            Text("2  hold the Teletype's front button and power on (screen stays dark)").font(mono(11)).foregroundColor(.txt)
                            Text("3  connect its USB port to the Mac with a USB A-to-A cable").font(mono(11)).foregroundColor(.txt)
                            Text("checked every 2 s").font(mono(10)).foregroundColor(.dimc).padding(.top, 2)
                        }.padding(10).frame(maxWidth: .infinity, alignment: .leading).background(Color.panel).cornerRadius(4)
                    }
                }

                // 2. the one setting that matters
                VStack(alignment: .leading, spacing: 8) {
                    stepHead("2", "SETTINGS")
                    Text("Disting EX at 65 (0x41), ER-301 at 0x31: fixed. On the Disting EX: Settings > I2C address = 65.")
                        .font(mono(10.5)).foregroundColor(.dimc).fixedSize(horizontal: false, vertical: true)
                    if m.ports.version >= 2 {
                        HStack(spacing: 8) {
                            Toggle(isOn: $m.ports.bootPresetOn) { Text("load Disting EX preset at power-on").font(mono(11)).foregroundColor(.txt) }
                                .toggleStyle(.checkbox).disabled(m.busy)
                            if m.ports.bootPresetOn {
                                TextField("", value: $m.ports.bootPreset, formatter: NumberFormatter()).textFieldStyle(.plain).font(mono(11)).foregroundColor(.txt)
                                    .frame(width: 50).padding(3).background(Color.panel).cornerRadius(2).disabled(m.busy)
                            }
                        }
                    }
                    if let p = portsProblem { Text("✕ " + p).font(mono(11, .semibold)).foregroundColor(.pink) }
                }

                // 3. flash
                VStack(alignment: .leading, spacing: 10) {
                    stepHead("3", "FLASH")
                    if m.firmware == .stock {
                        HStack(spacing: 10) {
                            Text("STOCK TELETYPE: " + ((m.stockHex as NSString?)?.lastPathComponent ?? "no file")).font(mono(11, .semibold)).foregroundColor(.amber)
                            Button("FLASH STOCK") { confirm = true }.buttonStyle(Key(tint: m.canFlash ? .amber : .dimc)).disabled(!m.canFlash)
                            Button("cancel") { m.firmware = .orca }.buttonStyle(.plain).font(mono(11)).foregroundColor(.dimc).disabled(m.busy)
                        }
                    } else {
                        Button(action: { confirm = true }) {
                            Text("FLASH ORCA SEAFLAKE").font(mono(15, .bold)).frame(maxWidth: .infinity).padding(.vertical, 10)
                                .foregroundColor(m.canFlash ? .bgc : .dimc).background(m.canFlash ? Color.cyan : Color.panel).cornerRadius(4)
                        }.buttonStyle(.plain).disabled(!m.canFlash).keyboardShortcut(.defaultAction)
                    }
                    if !m.canFlash && !m.busy && !whyNot.isEmpty { Text(whyNot).font(mono(11)).foregroundColor(.dimc) }
                    if m.busy || m.step > 0 {
                        ProgressView(value: m.step).progressViewStyle(.linear).tint(.cyan)
                        if m.busy { Text((m.log.last?.0 ?? "working…").trimmingCharacters(in: .whitespaces)).font(mono(10.5)).foregroundColor(.amber).lineLimit(1) }
                    }
                    if let (r, failed) = m.result {
                        Text(failed ? r : (m.step >= 1 ? "DONE. Unplug the USB cable and power-cycle the Teletype. When it says SCENES WILL BE OVERWRITTEN, press the front button once." : r))
                            .font(mono(11.5, .semibold)).foregroundColor(failed ? .pink : .cyan).padding(10)
                            .frame(maxWidth: .infinity, alignment: .leading).fixedSize(horizontal: false, vertical: true)
                            .background((failed ? Color.pink : Color.cyan).opacity(0.12)).cornerRadius(3)
                    }
                    if m.firmware == .orca {
                        Button("Restore stock Teletype…") { m.chooseStock() }.buttonStyle(.plain).font(mono(10.5)).foregroundColor(.dimc).disabled(m.busy)
                            .help("Choose ~/Downloads/teletype.hex (or monome's release .zip), then FLASH STOCK")
                    }
                }

                // everything else
                VStack(alignment: .leading, spacing: 10) {
                    Button(action: { details.toggle() }) {
                        Text((details ? "▾ " : "▸ ") + "DETAILS").font(mono(11, .semibold)).foregroundColor(.dimc)
                    }.buttonStyle(.plain)
                    if details {
                        Text("FIRMWARE  " + m.orcaInfo).font(mono(10)).foregroundColor(.dimc).textSelection(.enabled)
                        if !m.bootloader.isEmpty { Text("BOOTLOADER  " + m.bootloader).font(mono(10)).foregroundColor(.dimc) }
                        if let t = m.tool { Text("DFU-PROGRAMMER  " + t).font(mono(10)).foregroundColor(.dimc) }
                        if m.firmware == .orca { PortsTable(m: m) }
                        HStack(spacing: 10) {
                            Button("OPEN MONOME RELEASES") { NSWorkspace.shared.open(releasesURL) }.buttonStyle(Key(tint: .dimc))
                            Button(guide ? "HIDE FULL GUIDE" : "FULL FLASHING GUIDE") { guide.toggle() }.buttonStyle(Key(tint: .dimc))
                        }
                        if guide { Instructions(lines: m.instructions).frame(height: 360) }
                        Text("LOG").font(mono(11, .semibold)).foregroundColor(.dimc)
                        ScrollViewReader { sv in
                            ScrollView {
                                VStack(alignment: .leading, spacing: 1) {
                                    ForEach(Array(m.log.enumerated()), id: \.offset) { i, l in
                                        Text(l.0).font(mono(10.5)).foregroundColor(l.1).textSelection(.enabled).id(i)
                                    }
                                }.frame(maxWidth: .infinity, alignment: .leading).padding(8)
                            }.background(Color.panel).cornerRadius(4).frame(height: 200)
                            .onChange(of: m.log.count) { n in if n > 0 { sv.scrollTo(n - 1) } }
                        }
                    }
                }
            }.frame(width: w - 36, alignment: .leading).padding(18)
        }
        .background(Color.bgc)
        .frame(width: w, height: details ? min(900, screenH) : min(580, screenH))
        .alert(m.firmware == .orca ? "Flash Orca Seaflake onto the Teletype?" : "Flash the stock Teletype firmware?", isPresented: $confirm) {
            Button("Flash", role: .destructive) { m.flash() }
            Button("Cancel", role: .cancel) {}
        } message: { Text("This erases the module: Teletype scenes, Orca patches and calibration. Back them up first with WRITE TO USB." + (m.firmware == .orca ? "\n\nDisting EX at 0x41, ER-301 at 0x31 (fixed)." : "")) }
    }
    var whyNot: String {
        if m.tool == nil { return "install dfu-programmer first" }
        if !m.dfuPresent { return "waiting for the Teletype (step 1)" }
        if m.firmware == .stock { return m.stockHex == nil ? "choose the stock teletype.hex first" : "" }
        if m.orcaHex == nil { return "the Orca Seaflake hex is missing from the app" }
        return m.portsOK ? "" : "fix the address above (or tick flash anyway under DETAILS)"
    }
}
