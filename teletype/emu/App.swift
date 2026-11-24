// Orca Seaflake TT: the emulator app. SwiftUI chrome around the firmware's
// own C code (tt_emu.c + src/ + module/, built into libseaflake-emu.a).
//
// Left: the Teletype OLED, pixel for pixel (128 x 64 at 6x, each OLED pixel
// a 5 px square with a 1 px gap, 16 warm-white levels). Below it TR 1-4,
// CV 1-4 and the 26 ER-301 ports. Right: everything sent on the I2C bus, the
// Teletype commands that sent it, and the MIDI the Disting EX would play.
// That MIDI also goes out of a virtual CoreMIDI source "Orca Seaflake TT".
// cmd+F opens the Flash window (Flash.swift), cmd+/ the Help window.
import SwiftUI
import AppKit
import CoreMIDI

extension Color {
    static let bgc = Color(red: 12/255, green: 12/255, blue: 14/255)
    static let panel = Color(red: 22/255, green: 22/255, blue: 25/255)
    static let dimc = Color(red: 110/255, green: 110/255, blue: 118/255)
    static let txt = Color(red: 220/255, green: 220/255, blue: 220/255)
    static let cyan = Color(red: 114/255, green: 222/255, blue: 194/255)
    static let amber = Color(red: 255/255, green: 184/255, blue: 0)
    static let pink = Color(red: 255/255, green: 110/255, blue: 160/255)
}

func mono(_ s: CGFloat = 12, _ w: Font.Weight = .regular) -> Font { .system(size: s, weight: w, design: .monospaced) }

struct Key: ButtonStyle {
    var tint: Color = .cyan
    func makeBody(configuration c: Configuration) -> some View {
        c.label.font(mono(11, .semibold)).foregroundColor(.bgc).padding(.horizontal, 12).padding(.vertical, 5)
            .background(tint.opacity(c.isPressed ? 0.6 : 1)).cornerRadius(3)
    }
}

struct Pill: View {
    let text: String; var color: Color = .cyan
    var body: some View { Text(text).font(mono(11, .semibold)).foregroundColor(color) }
}

extension Notification.Name {
    static let openFlash = Notification.Name("openFlash")
    static let openHelp = Notification.Name("openHelp")
}

func nowMs() -> UInt32 { UInt32(truncatingIfNeeded: DispatchTime.now().uptimeNanoseconds / 1_000_000) }

// the firmware's resource files (same text as the manual): in the app, or next to emu/ when run from the build
func resource(_ name: String) -> String? {
    let res = Bundle.main.resourcePath ?? "."
    for p in [res + "/" + name, res + "/../../../../docs/manual/" + name, "../docs/manual/" + name]
        where FileManager.default.isReadableFile(atPath: p) { return p }
    return nil
}

////////////////////////////////////////////////////////////////////////////
// the emulator core on its own thread

struct LogLine: Identifiable { let id: Int; let kind: Int; let text: String }

// the C callbacks land here (not on Emu.shared: they already run while it is being made)
var logPending: [(Int, String)] = []
let logLock = NSLock()
var midiSrc = MIDIEndpointRef()
func enqueueLog(_ kind: Int, _ text: String) { logLock.lock(); logPending.append((kind, text)); logLock.unlock() }
func midiOut(_ b: [UInt8]) {
    guard midiSrc != 0, b.count <= 3 else { return }
    var buf = [UInt8](repeating: 0, count: 64)
    buf.withUnsafeMutableBytes { raw in
        let pl = raw.baseAddress!.assumingMemoryBound(to: MIDIPacketList.self)
        var p = MIDIPacketListInit(pl)
        p = MIDIPacketListAdd(pl, 64, p, 0, b.count, b)
        MIDIReceived(midiSrc, pl)
    }
}

final class Emu: ObservableObject {
    static let shared = Emu()
    let lock = NSLock()
    @Published var fb = [UInt8](repeating: 0, count: 128 * 64)
    @Published var tr = [Bool](repeating: false, count: 4)
    @Published var cv = [Int16](repeating: 0, count: 4)
    @Published var erTr = [Bool](repeating: false, count: 26)
    @Published var erCv = [Int16](repeating: 0, count: 26)
    @Published var erSeen = [Bool](repeating: false, count: 26)
    @Published var bpm = 120
    @Published var frame: UInt32 = 0
    @Published var playing = false
    @Published var mode = 6
    @Published var log: [LogLine] = []
    @Published var logPaused = false
    private var nextId = 0
    let dir: String
    private var midiClient = MIDIClientRef()

    private init() {
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        dir = home + "/Library/Application Support/Orca Seaflake Teletype"
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        if MIDIClientCreateWithBlock("Orca Seaflake TT" as CFString, &midiClient, { _ in }) == noErr {
            MIDISourceCreate(midiClient, "Orca Seaflake TT" as CFString, &midiSrc)
        }
        emu_set_log { kind, text in
            guard let text = text else { return }
            enqueueLog(Int(kind), String(cString: text))
        }
        emu_set_midi { bytes, n in
            guard let bytes = bytes, n > 0 else { return }
            midiOut(Array(UnsafeBufferPointer(start: bytes, count: Int(n))))
        }
        emu_init(dir, nowMs())
        enqueue(3, "ORCA SEAFLAKE READY. IT IS PLAYING: SPACE STOPS, ALT-H HELP ON THE OLED, CMD-/ HELP HERE, CMD-F FLASH.")
        enqueue(3, dir)
        let t = Thread { [unowned self] in
            while true {
                self.lock.lock(); emu_tick(nowMs()); self.lock.unlock()
                usleep(400)
            }
        }
        t.qualityOfService = .userInteractive
        t.start()
        Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [unowned self] _ in self.snapshot() }
    }

    func enqueue(_ kind: Int, _ text: String) { enqueueLog(kind, text) }

    // called at 60 Hz on the main thread: the OLED and the outputs
    private func snapshot() {
        lock.lock()
        emu_refresh()
        let f = emu_framebuffer()!
        fb = Array(UnsafeBufferPointer(start: f, count: 128 * 64))
        for i in 0..<4 { tr[i] = emu_tr(Int32(i)); cv[i] = emu_cv(Int32(i)) }
        for p in 1...26 { erTr[p - 1] = emu_er_tr(Int32(p)); erCv[p - 1] = emu_er_cv(Int32(p)); erSeen[p - 1] = emu_er_seen(Int32(p)) != 0 }
        bpm = Int(emu_bpm()); frame = emu_frame(); playing = emu_playing(); mode = Int(emu_mode())
        lock.unlock()
        logLock.lock(); let p = logPending; logPending = []; logLock.unlock()
        for (k, t) in p where !logPaused || k == 3 {
            log.append(LogLine(id: nextId, kind: k, text: t)); nextId += 1
        }
        if log.count > 400 { log.removeFirst(log.count - 400) }
    }

    func key(_ hid: UInt8, _ mod: UInt8, _ rep: Bool) { lock.lock(); emu_key(hid, mod, rep); lock.unlock() }
    func keyUp(_ hid: UInt8, _ mod: UInt8) { lock.lock(); emu_key_release(hid, mod); lock.unlock() }
    // the module's front-panel button (LOAD PATCH, press again to load) and PARAM knob
    func front() { lock.lock(); emu_front(); lock.unlock() }
    func knob(_ v: UInt16) { lock.lock(); emu_knob(v); lock.unlock() }
    // the ports configuration block the Flash window will write (startup preset...)
    func setConfig(_ b: [UInt8]) { guard b.count == 32 else { return }; lock.lock(); emu_set_config(b); lock.unlock() }
    func dream() { lock.lock(); emu_set_idle(5 * 60 * 1000); lock.unlock(); enqueue(3, "SCREENSAVER: THE TRAIN DREAM OF 2024-10-08 (ANY KEY WAKES IT)") }
    func importUSB() {
        var s = [CChar](repeating: 0, count: 200)
        lock.lock(); let r = emu_import_usb(&s, 200); lock.unlock()
        enqueue(3, r == 0 ? "NO SFSCAN.TXT / SFPATCH.TXT ON ANY VOLUME IN /VOLUMES (WRITE TO USB ON THE MODULE FIRST)" : String(cString: s))
    }
    func importFolder() {
        let pnl = NSOpenPanel(); pnl.canChooseDirectories = true; pnl.canChooseFiles = false
        pnl.directoryURL = URL(fileURLWithPath: "/Volumes"); pnl.message = "Choose the USB stick or folder with SFSCAN.TXT / SFPATCH.TXT"
        guard pnl.runModal() == .OK, let u = pnl.url else { return }
        var s = [CChar](repeating: 0, count: 200)
        lock.lock(); let r = emu_import_dir(u.path, &s, 200); lock.unlock()
        enqueue(3, r == 0 ? "NOTHING TO IMPORT IN \(u.path)" : String(cString: s))
    }
    func exportFolder() {
        let pnl = NSOpenPanel(); pnl.canChooseDirectories = true; pnl.canChooseFiles = false; pnl.canCreateDirectories = true
        pnl.directoryURL = URL(fileURLWithPath: "/Volumes"); pnl.message = "Choose the USB stick (or a folder) to write SFSCAN.TXT + SFPATCH.TXT into"
        pnl.prompt = "Export"
        guard pnl.runModal() == .OK, let u = pnl.url else { return }
        var s = [CChar](repeating: 0, count: 300)
        lock.lock(); _ = emu_export_dir(u.path, &s, 300); lock.unlock()
        enqueue(3, String(cString: s))
    }
}

////////////////////////////////////////////////////////////////////////////
// the OLED, pixel-accurate, and the keyboard

final class OLEDNSView: NSView {
    var fb = [UInt8](repeating: 0, count: 128 * 64) { didSet { needsDisplay = true } }
    private var pixels = [UInt8](repeating: 0, count: 768 * 384 * 4)
    override var acceptsFirstResponder: Bool { true }
    override var isOpaque: Bool { true }
    override func viewDidMoveToWindow() { window?.makeFirstResponder(self) }
    override func mouseDown(with e: NSEvent) { window?.makeFirstResponder(self) }

    override func draw(_ r: NSRect) {
        // exactly what the SDL emulator drew: 5x5 lit squares on a 6 px grid, warm white, 16 levels
        pixels.withUnsafeMutableBytes { raw in
            let p = raw.baseAddress!.assumingMemoryBound(to: UInt32.self)
            for i in 0..<(768 * 384) { p[i] = 0xFF0C0C0E }
            for y in 0..<64 { for x in 0..<128 {
                let v = Int(fb[y * 128 + x]); if v == 0 { continue }
                let l = v * 17, g = l * 245 / 255, b = l * 225 / 255
                let c = UInt32(0xFF000000) | UInt32(l) << 16 | UInt32(g) << 8 | UInt32(b)   // XRGB, little-endian = BGRA in memory
                for dy in 0..<5 { let row = (y * 6 + dy) * 768 + x * 6; for dx in 0..<5 { p[row + dx] = c } }
            } }
        }
        guard let ctx = NSGraphicsContext.current?.cgContext,
              let data = CGDataProvider(data: Data(pixels) as CFData),
              let img = CGImage(width: 768, height: 384, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 768 * 4,
                                space: CGColorSpaceCreateDeviceRGB(),
                                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue),
                                provider: data, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return }
        ctx.interpolationQuality = .none
        ctx.draw(img, in: CGRect(x: 0, y: 0, width: 768, height: 384))
    }

    // macOS virtual key codes -> USB HID usage codes (what the module's keyboard sends)
    static let hid: [UInt16: UInt8] = [
        0x00: 0x04, 0x0B: 0x05, 0x08: 0x06, 0x02: 0x07, 0x0E: 0x08, 0x03: 0x09, 0x05: 0x0A, 0x04: 0x0B, 0x22: 0x0C, 0x26: 0x0D,
        0x28: 0x0E, 0x25: 0x0F, 0x2E: 0x10, 0x2D: 0x11, 0x1F: 0x12, 0x23: 0x13, 0x0C: 0x14, 0x0F: 0x15, 0x01: 0x16, 0x11: 0x17,
        0x20: 0x18, 0x09: 0x19, 0x0D: 0x1A, 0x07: 0x1B, 0x10: 0x1C, 0x06: 0x1D,
        0x12: 0x1E, 0x13: 0x1F, 0x14: 0x20, 0x15: 0x21, 0x17: 0x22, 0x16: 0x23, 0x1A: 0x24, 0x1C: 0x25, 0x19: 0x26, 0x1D: 0x27,
        0x24: 0x28, 0x35: 0x29, 0x33: 0x2A, 0x30: 0x2B, 0x31: 0x2C, 0x1B: 0x2D, 0x18: 0x2E, 0x21: 0x2F, 0x1E: 0x30, 0x2A: 0x31,
        0x29: 0x33, 0x27: 0x34, 0x32: 0x35, 0x2B: 0x36, 0x2F: 0x37, 0x2C: 0x38, 0x39: 0x39,
        0x7A: 0x3A, 0x78: 0x3B, 0x63: 0x3C, 0x76: 0x3D, 0x60: 0x3E, 0x61: 0x3F, 0x62: 0x40, 0x64: 0x41, 0x65: 0x42, 0x6D: 0x43, 0x67: 0x44, 0x6F: 0x45,
        0x73: 0x4A, 0x74: 0x4B, 0x75: 0x4C, 0x77: 0x4D, 0x79: 0x4E, 0x7C: 0x4F, 0x7B: 0x50, 0x7D: 0x51, 0x7E: 0x52, 0x0A: 0x64,
    ]
    static func mod(_ e: NSEvent) -> UInt8 {
        let f = e.modifierFlags.rawValue
        var m: UInt8 = 0
        if f & 0x0001 != 0 { m |= 0x01 }   // left control
        if f & 0x0002 != 0 { m |= 0x02 }   // left shift
        if f & 0x0020 != 0 { m |= 0x04 }   // left option
        if f & 0x0008 != 0 { m |= 0x08 }   // left command
        if f & 0x2000 != 0 { m |= 0x10 }   // right control
        if f & 0x0004 != 0 { m |= 0x20 }   // right shift
        if f & 0x0040 != 0 { m |= 0x40 }   // right option
        if f & 0x0010 != 0 { m |= 0x80 }   // right command
        return m
    }
    override func keyDown(with e: NSEvent) {
        if e.modifierFlags.contains(.command) { super.keyDown(with: e); return }   // cmd keys belong to the menu
        guard let h = OLEDNSView.hid[e.keyCode] else { return }
        Emu.shared.key(h, OLEDNSView.mod(e), e.isARepeat)
    }
    override func keyUp(with e: NSEvent) {
        guard let h = OLEDNSView.hid[e.keyCode] else { return }
        Emu.shared.keyUp(h, OLEDNSView.mod(e))
    }
    override func flagsChanged(with e: NSEvent) {
        // caps lock is a key on the Teletype too
        if e.keyCode == 0x39 { Emu.shared.key(0x39, OLEDNSView.mod(e), false); Emu.shared.keyUp(0x39, OLEDNSView.mod(e)) }
    }
}

struct OLEDView: NSViewRepresentable {
    let fb: [UInt8]
    func makeNSView(context: Context) -> OLEDNSView { OLEDNSView(frame: NSRect(x: 0, y: 0, width: 768, height: 384)) }
    func updateNSView(_ v: OLEDNSView, context: Context) { v.fb = fb }
}

////////////////////////////////////////////////////////////////////////////
// the main window

struct Meter: View {
    let w: CGFloat, value: CGFloat, color: Color, h: CGFloat
    var body: some View {
        ZStack(alignment: .leading) {
            Rectangle().fill(Color(red: 40/255, green: 40/255, blue: 44/255)).frame(width: w, height: h)
            Rectangle().fill(color).frame(width: max(0, min(w, value * w)), height: h)
        }
    }
}

struct MainView: View {
    @ObservedObject var emu = Emu.shared
    @ObservedObject var fl = FlashModel.shared
    @Environment(\.openWindow) var openWindow
    static let modes = ["TELETYPE LIVE", "TELETYPE EDIT", "TELETYPE PATTERN", "TELETYPE SCENE WRITE", "TELETYPE SCENE READ", "HELP", "GRID"]
    var body: some View {
        HStack(alignment: .top, spacing: 18) {
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 14) {
                    Text("ORCA SEAFLAKE").font(mono(16, .bold)).foregroundColor(.cyan)
                    Text(emu.mode >= 0 && emu.mode < 7 ? MainView.modes[emu.mode] : "").font(mono(12, .semibold)).foregroundColor(emu.mode == 6 ? .dimc : .amber)
                    Spacer()
                    Text("\(emu.playing ? "PLAY" : "STOP")  \(emu.bpm) BPM  F\(emu.frame)").font(mono(12, .semibold)).foregroundColor(emu.playing ? .cyan : .dimc)
                }
                OLEDView(fb: emu.fb).frame(width: 768, height: 384)
                    .padding(4).background(Color.bgc).overlay(Rectangle().stroke(Color(red: 40/255, green: 40/255, blue: 44/255), lineWidth: 1))
                HStack(spacing: 24) {
                    ForEach(0..<4, id: \.self) { i in
                        HStack(spacing: 8) {
                            Text("TR\(i + 1)").font(mono(11, .semibold)).foregroundColor(.dimc)
                            Rectangle().fill(emu.tr[i] ? Color.amber : Color(red: 50/255, green: 50/255, blue: 54/255)).frame(width: 14, height: 14)
                            VStack(alignment: .leading, spacing: 3) {
                                Text(String(format: "CV%d %5.2fV", i + 1, Double(emu.cv[i]) * 10 / 16384)).font(mono(11)).foregroundColor(.dimc)
                                Meter(w: 110, value: CGFloat(emu.cv[i]) / 16384, color: .cyan, h: 6)
                            }
                        }
                    }
                }
                Text("ER-301 PORTS (SC.TR / SC.CV)").font(mono(11, .semibold)).foregroundColor(.dimc)
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(0..<2, id: \.self) { row in
                        HStack(spacing: 10) {
                            ForEach(0..<13, id: \.self) { col in
                                let p = row * 13 + col
                                VStack(alignment: .leading, spacing: 2) {
                                    Text("\(Character(UnicodeScalar(97 + p)!))\(p + 1)").font(mono(10)).foregroundColor(emu.erSeen[p] ? .txt : .dimc)
                                    HStack(spacing: 3) {
                                        Rectangle().fill(emu.erTr[p] ? Color.amber : Color(red: 50/255, green: 50/255, blue: 54/255)).frame(width: 10, height: 10)
                                        Meter(w: 36, value: CGFloat(emu.erCv[p]) / 16384, color: .pink, h: 10)
                                    }
                                }.frame(width: 50, alignment: .leading)
                            }
                        }
                    }
                }
                HStack(spacing: 10) {
                    Button("FLASH…") { openWindow(id: "flash") }.buttonStyle(Key())
                    Button("HELP") { openWindow(id: "help") }.buttonStyle(Key(tint: .dimc))
                    Button("IMPORT USB") { emu.importUSB() }.buttonStyle(Key(tint: .dimc))
                    Button("EXPORT…") { emu.exportFolder() }.buttonStyle(Key(tint: .dimc))
                    Pill(text: fl.statusLine, color: fl.statusColor)
                }
            }
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text(emu.logPaused ? "I2C LOG (PAUSED)" : "I2C LOG").font(mono(12, .semibold)).foregroundColor(emu.logPaused ? .amber : .dimc)
                    Spacer()
                    Button(emu.logPaused ? "RESUME" : "PAUSE") { emu.logPaused.toggle() }.buttonStyle(Key(tint: .dimc))
                    Button("CLEAR") { emu.log = [] }.buttonStyle(Key(tint: .dimc))
                }
                ScrollViewReader { sv in
                    ScrollView {
                        VStack(alignment: .leading, spacing: 1) {
                            ForEach(emu.log) { l in
                                Text(l.text).font(mono(10.5)).foregroundColor(l.kind == 0 ? .cyan : l.kind == 2 ? .amber : l.kind == 3 ? .txt : .dimc)
                                    .textSelection(.enabled).id(l.id)
                            }
                        }.frame(maxWidth: .infinity, alignment: .leading).padding(8)
                    }.background(Color.panel).cornerRadius(4)
                    .onChange(of: emu.log.last?.id) { id in if let id = id { sv.scrollTo(id) } }
                }
                Text("SPACE PLAY/STOP   ALT-H HELP ON THE OLED   CTRL-S/O PATCHES   CTRL-T SONG   CTRL-I SCAN   CMD-B FRONT BUTTON").font(mono(10)).foregroundColor(.dimc)
                Text("CMD-F FLASH   CMD-/ HELP   CMD-I IMPORT   CMD-E EXPORT   CMD-D DREAM   CMD-L CLEAR   CMD-P PAUSE   MIDI OUT: \"ORCA SEAFLAKE TT\"").font(mono(10)).foregroundColor(.dimc)
            }.frame(width: 540)
        }
        .padding(18).background(Color.bgc)
        .onAppear {   // --flash / --help on the command line open those windows too (screenshots)
            let a = ProcessInfo.processInfo.arguments
            if a.contains("--flash") { openWindow(id: "flash") }
            if a.contains("--help") { openWindow(id: "help") }
        }
        .onReceive(NotificationCenter.default.publisher(for: .openFlash)) { _ in openWindow(id: "flash") }
        .onReceive(NotificationCenter.default.publisher(for: .openHelp)) { _ in openWindow(id: "help") }
    }
}

////////////////////////////////////////////////////////////////////////////
// help: keys, every operator, the flashing steps (docs/manual/*.tsv + flashing.txt)

struct Row: Identifiable { let id: Int; let cells: [String] }

func readTSV(_ name: String) -> [Row] {
    guard let p = resource(name), let t = try? String(contentsOfFile: p, encoding: .utf8) else { return [] }
    return t.split(whereSeparator: { $0.isNewline }).dropFirst().enumerated().map { Row(id: $0.offset, cells: $0.element.components(separatedBy: "\t")) }
}

struct HelpView: View {
    @State var search = ""
    @State var tab = 0
    let ops = readTSV("ops.tsv"), keys = readTSV("keys.tsv")
    let flashing = (try? String(contentsOfFile: resource("flashing.txt") ?? "", encoding: .utf8))?.components(separatedBy: "\n") ?? []
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("ORCA SEAFLAKE TT HELP").font(mono(16, .bold)).foregroundColor(.cyan)
                Spacer()
                ForEach(Array(["OPERATORS", "KEYS", "FLASHING"].enumerated()), id: \.offset) { i, n in
                    Button(n) { tab = i }.buttonStyle(Key(tint: tab == i ? .cyan : .dimc))
                }
                TextField("search", text: $search).textFieldStyle(.plain).font(mono(12)).foregroundColor(.txt).padding(6).frame(width: 200)
                    .background(Color.panel).cornerRadius(3)
            }
            if tab == 2 { Instructions(lines: flashing) }
            else { table(tab == 0 ? ops : keys, head: tab == 0 ? ["GLYPH", "NAME", "INPUTS", "OUTPUTS", "WHAT IT DOES"] : ["KEY", "WHERE", "DOES"]) }
        }.padding(18).background(Color.bgc).frame(minWidth: 1100, minHeight: 700)
    }
    func matches(_ r: Row) -> Bool { search.isEmpty || r.cells.contains { $0.localizedCaseInsensitiveContains(search) } }
    func table(_ rows: [Row], head: [String]) -> some View {
        let isOps = head.count == 5
        let widths: [CGFloat] = isOps ? [70, 130, 300, 190, 0] : [200, 190, 0]
        return ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                HStack(alignment: .top, spacing: 10) {
                    ForEach(Array(head.enumerated()), id: \.offset) { i, h in
                        Text(h).font(mono(10, .semibold)).foregroundColor(.dimc).frame(width: widths[i] == 0 ? nil : widths[i], alignment: .leading)
                            .frame(maxWidth: widths[i] == 0 ? .infinity : nil, alignment: .leading)
                    }
                }.padding(.vertical, 6)
                ForEach(Array(rows.filter(matches).enumerated()), id: \.element.id) { i, r in
                    let group = isOps ? r.cells[1] : ""
                    let prev = i == 0 ? "" : (isOps ? rows.filter(matches)[i - 1].cells[1] : "")
                    if isOps && group != prev {
                        Text(group).font(mono(12, .bold)).foregroundColor(.amber).padding(.top, 12).padding(.bottom, 4)
                    }
                    let cells = isOps ? [r.cells[0], r.cells[2], r.cells[3], r.cells[4], r.cells[5]] : r.cells
                    HStack(alignment: .top, spacing: 10) {
                        ForEach(Array(cells.enumerated()), id: \.offset) { j, c in
                            Text(c).font(mono(11, j == 0 ? .bold : .regular)).foregroundColor(j == 0 ? .cyan : j == cells.count - 1 ? .txt : .dimc)
                                .frame(width: widths[j] == 0 ? nil : widths[j], alignment: .leading)
                                .frame(maxWidth: widths[j] == 0 ? .infinity : nil, alignment: .leading)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }.padding(.vertical, 3)
                    Rectangle().fill(Color.panel).frame(height: 1)
                }
            }.padding(10)
        }.background(Color.panel.opacity(0.5)).cornerRadius(4)
    }
}

////////////////////////////////////////////////////////////////////////////

@main struct SeaflakeApp: App {
    var body: some Scene {
        Window("Orca Seaflake TT Alt", id: "main") { MainView().preferredColorScheme(.dark) }
            .windowResizability(.contentSize)
            .commands {
                CommandMenu("Emulator") {
                    Button("Flash the Teletype…") { NotificationCenter.default.post(name: .openFlash, object: nil) }.keyboardShortcut("f")
                    Button("Flash the Teletype… (cmd-U)") { NotificationCenter.default.post(name: .openFlash, object: nil) }.keyboardShortcut("u")
                    Button("Help: keys, operators, flashing") { NotificationCenter.default.post(name: .openHelp, object: nil) }.keyboardShortcut("/")
                    Divider()
                    Button("Import from USB stick (SFSCAN.TXT + SFPATCH.TXT)") { Emu.shared.importUSB() }.keyboardShortcut("i")
                    Button("Import from a folder…") { Emu.shared.importFolder() }.keyboardShortcut("i", modifiers: [.command, .shift])
                    Button("Export patches + songs to a USB stick…") { Emu.shared.exportFolder() }.keyboardShortcut("e")
                    Divider()
                    Button("Front-panel button (LOAD PATCH)") { Emu.shared.front() }.keyboardShortcut("b")
                    Button("Screensaver now (the dream)") { Emu.shared.dream() }.keyboardShortcut("d")
                    Button("Clear the log") { Emu.shared.log = [] }.keyboardShortcut("l")
                    Button("Pause / resume the log") { Emu.shared.logPaused.toggle() }.keyboardShortcut("p")
                }
            }
        Window("Flash the Teletype", id: "flash") { FlashView().preferredColorScheme(.dark) }.windowResizability(.contentSize)
        Window("Orca Seaflake TT Help", id: "help") { HelpView().preferredColorScheme(.dark) }
    }
}
