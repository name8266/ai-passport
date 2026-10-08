import Foundation

/// Protocol v1. Exactly 20 bytes, little-endian, XOR over bytes 0...18.
/// Same as firmware/main/nav_packet.h.
struct NavSnapshot: Equatable {
    var maneuver: UInt8 = 0
    var isNavigating = false
    var hasGPS = false
    var rerouting = false
    var turnMeters: UInt16 = 0
    var speedTenthsKmh: UInt16 = 0
    var remainingTensMeters: UInt16 = 0
    var remainingSeconds: UInt16 = 0
    var etaHour: UInt8 = 0
    var etaMinute: UInt8 = 0
    var bearingDegrees: UInt16 = 0

    static var demonstration: NavSnapshot {
        NavSnapshot(maneuver: 3, isNavigating: true, hasGPS: true,
                    turnMeters: 350, speedTenthsKmh: 480,
                    remainingTensMeters: 1260, remainingSeconds: 1100,
                    etaHour: 18, etaMinute: 42, bearingDegrees: 90)
    }

    func encode(sequence: UInt16) -> Data {
        var b: [UInt8] = [0xA5, 1, min(maneuver, 7)]
        b.append((isNavigating ? 1 : 0) | (hasGPS ? 2 : 0) | (rerouting ? 4 : 0))
        func put16(_ value: UInt16) {
            b.append(UInt8(truncatingIfNeeded: value))
            b.append(UInt8(truncatingIfNeeded: value >> 8))
        }
        put16(turnMeters)
        put16(speedTenthsKmh)
        put16(remainingTensMeters)
        put16(remainingSeconds)
        b.append(min(etaHour, 23))
        b.append(min(etaMinute, 59))
        put16(sequence)
        put16(min(bearingDegrees, 359))
        b.append(0)
        b.append(b.reduce(0, ^))
        assert(b.count == 20)
        return Data(b)
    }
}
