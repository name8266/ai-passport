import Combine
import CoreLocation
import MapKit
import Foundation

/// All map and GPS work stays on iPhone. Passport receives only derived telemetry.
@MainActor
final class NavigationModel: NSObject, ObservableObject, CLLocationManagerDelegate {
    @Published private(set) var destination: MKMapItem?
    @Published private(set) var route: MKRoute?
    @Published private(set) var location: CLLocation?
    @Published private(set) var navigating = false
    @Published private(set) var status = "正在请求定位"
    @Published private(set) var stepText = "等待路线"
    @Published private(set) var remainingMeters: Double = 0
    @Published private(set) var turnMeters: Double = 0
    @Published private(set) var eta: Date = Date()
    @Published private(set) var rerouting = false

    var onTelemetry: ((NavSnapshot) -> Void)?
    private let gps = CLLocationManager()
    private var stepIndex = 0
    private var progressedMeters: Double = 0
    private var lastReroute = Date.distantPast
    private var offRouteSince: Date?

    override init() {
        super.init()
        gps.delegate = self
        gps.desiredAccuracy = kCLLocationAccuracyBestForNavigation
        gps.distanceFilter = 3
        gps.activityType = .automotiveNavigation
        gps.pausesLocationUpdatesAutomatically = false
        gps.requestWhenInUseAuthorization()
        if CLLocationManager.locationServicesEnabled() {
            gps.startUpdatingLocation()
        }
    }

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        switch manager.authorizationStatus {
        case .authorizedAlways, .authorizedWhenInUse:
            gps.startUpdatingLocation()
            status = "定位已授权"
        case .denied, .restricted:
            status = "请在设置中允许定位"
        default:
            status = "等待定位授权"
        }
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        status = "定位错误：\(error.localizedDescription)"
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let point = locations.last, point.horizontalAccuracy >= 0,
              point.horizontalAccuracy < 150 else { return }
        if navigating { updateProgress(point) }
        location = point
        onTelemetry?(snapshot)
    }

    func findDestination(_ query: String) async {
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        status = "搜索目的地…"
        let request = MKLocalSearch.Request()
        request.naturalLanguageQuery = trimmed
        if let location {
            request.region = MKCoordinateRegion(center: location.coordinate,
                                                latitudinalMeters: 100_000,
                                                longitudinalMeters: 100_000)
        }
        do {
            let result = try await MKLocalSearch(request: request).start()
            guard let first = result.mapItems.first else {
                status = "未找到目的地"
                return
            }
            destination = first
            status = "目的地：\(first.name ?? trimmed)"
        } catch {
            status = "搜索失败：\(error.localizedDescription)"
        }
    }

    func startNavigation() async {
        guard let here = location, let destination else {
            status = "请先定位并搜索目的地"
            return
        }
        status = "正在规划路线…"
        let request = MKDirections.Request()
        request.source = MKMapItem(placemark: MKPlacemark(coordinate: here.coordinate))
        request.destination = destination
        request.transportType = .automobile
        request.requestsAlternateRoutes = false
        do {
            guard let first = try await MKDirections(request: request).calculate().routes.first else {
                status = "未找到可行驶路线"
                return
            }
            guard !first.steps.isEmpty else { status = "路线缺少转向步骤"; return }
            installRoute(first)
            navigating = true
            gps.allowsBackgroundLocationUpdates = true
            status = "导航中"
            updateProgress(here)
        } catch {
            status = "规划失败：\(error.localizedDescription)"
        }
    }

    private func installRoute(_ newRoute: MKRoute) {
        route = newRoute
        progressedMeters = 0
        stepIndex = nextMeaningfulStep(from: 0, in: newRoute)
        remainingMeters = newRoute.distance
        turnMeters = newRoute.steps[stepIndex].distance
        stepText = newRoute.steps[stepIndex].instructions
        eta = Date().addingTimeInterval(newRoute.expectedTravelTime)
        offRouteSince = nil
    }

    private func nextMeaningfulStep(from index: Int, in route: MKRoute) -> Int {
        guard !route.steps.isEmpty else { return 0 }
        return (index..<route.steps.count).first {
            !route.steps[$0].instructions.isEmpty && route.steps[$0].distance > 0
        } ?? min(index, route.steps.count - 1)
    }

    /// Project position onto route polyline (nearest segment). Returns distance along
    /// polyline and lateral distance; avoids naive straight-line destination metrics.
    private func positionOnRoute(_ here: CLLocation, line: MKPolyline) -> (Double, Double)? {
        guard line.pointCount > 1 else { return nil }
        let points = line.points()
        let herePoint = MKMapPoint(here.coordinate)
        let metersPerUnit = MKMetersPerMapPointAtLatitude(here.coordinate.latitude)
        var bestOff = Double.infinity
        var bestAlong = 0.0
        var accumulated = 0.0
        for i in 0..<(line.pointCount - 1) {
            let a = points[i], b = points[i + 1]
            let dx = b.x - a.x, dy = b.y - a.y
            let len2 = dx * dx + dy * dy
            if len2 == 0 { continue }
            let dot = (herePoint.x - a.x) * dx + (herePoint.y - a.y) * dy
            let t = max(0, min(1, dot / len2))
            let lateral = hypot(herePoint.x - (a.x + t * dx),
                                herePoint.y - (a.y + t * dy)) * metersPerUnit
            let length = sqrt(len2) * metersPerUnit
            if lateral < bestOff {
                bestOff = lateral
                bestAlong = accumulated + length * t
            }
            accumulated += length
        }
        return (bestAlong, bestOff)
    }

    private func updateProgress(_ here: CLLocation) {
        guard let route, !route.steps.isEmpty else { return }
        if let (along, off) = positionOnRoute(here, line: route.polyline) {
            if off > 100 {
                if offRouteSince == nil { offRouteSince = Date() }
                if let since = offRouteSince,
                   Date().timeIntervalSince(since) > 8,
                   Date().timeIntervalSince(lastReroute) > 25 {
                    lastReroute = Date()
                    offRouteSince = nil
                    Task { await recalculate(from: here) }
                }
            } else {
                offRouteSince = nil
                progressedMeters = max(progressedMeters, min(route.distance, along))
            }
        }
        remainingMeters = max(0, route.distance - progressedMeters)
        eta = Date().addingTimeInterval(
            route.expectedTravelTime * (remainingMeters / max(1, route.distance)))
        // A maneuver is completed after approaching the corresponding step endpoint.
        if stepIndex < route.steps.count - 1 {
            let polyline = route.steps[stepIndex].polyline
            if polyline.pointCount > 0 {
                let endpoint = polyline.points()[polyline.pointCount - 1].coordinate
                let toEnd = here.distance(from: CLLocation(latitude: endpoint.latitude,
                                                          longitude: endpoint.longitude))
                if toEnd < 28 { stepIndex = nextMeaningfulStep(from: stepIndex + 1, in: route) }
            }
        }
        let step = route.steps[stepIndex]
        stepText = step.instructions.isEmpty ? "按路线行驶" : step.instructions
        let polyline = step.polyline
        if polyline.pointCount > 0 {
            let p = polyline.points()[polyline.pointCount - 1].coordinate
            turnMeters = here.distance(from: CLLocation(latitude: p.latitude,
                                                        longitude: p.longitude))
        } else {
            turnMeters = remainingMeters
        }
    }

    private func recalculate(from point: CLLocation) async {
        guard let destination, !rerouting, navigating else { return }
        rerouting = true
        status = "检测到偏航，重新规划…"
        defer { rerouting = false }
        let request = MKDirections.Request()
        request.source = MKMapItem(placemark: MKPlacemark(coordinate: point.coordinate))
        request.destination = destination
        request.transportType = .automobile
        do {
            if let newRoute = try await MKDirections(request: request).calculate().routes.first,
               !newRoute.steps.isEmpty {
                installRoute(newRoute)
                status = "路线已更新"
            }
        } catch {
            status = "重新规划失败，保留原路线"
        }
    }

    func stop() {
        navigating = false
        route = nil
        gps.allowsBackgroundLocationUpdates = false
        remainingMeters = 0
        turnMeters = 0
        stepText = "未开始导航"
        status = "导航已结束"
    }

    var snapshot: NavSnapshot {
        let point = location
        let freshFix = point.map { abs($0.timestamp.timeIntervalSinceNow) < 6 && $0.horizontalAccuracy >= 0 && $0.horizontalAccuracy < 100 } ?? false
        let speed = freshFix ? max(0, point?.speed ?? 0) * 3.6 : 0
        let course = point?.course ?? -1
        let time = Calendar.current.dateComponents([.hour, .minute], from: eta)
        return NavSnapshot(
            maneuver: navigating ? classify(stepText) : 0,
            isNavigating: navigating, hasGPS: freshFix,
            rerouting: rerouting,
            turnMeters: UInt16(clamping: Int(max(0, turnMeters.rounded()))),
            speedTenthsKmh: UInt16(clamping: Int((speed * 10).rounded())),
            remainingTensMeters: UInt16(clamping: Int((remainingMeters / 10).rounded())),
            remainingSeconds: UInt16(clamping: Int(max(0, eta.timeIntervalSinceNow.rounded()))),
            etaHour: UInt8(clamping: time.hour ?? 0),
            etaMinute: UInt8(clamping: time.minute ?? 0),
            bearingDegrees: UInt16(clamping: course < 0 ? 0 : Int(course.rounded())) % 360
        )
    }

    private func classify(_ instruction: String) -> UInt8 {
        let s = instruction.lowercased()
        if s.contains("到达") || s.contains("arrive") { return 5 }
        if s.contains("掉头") || s.contains("u-turn") { return 4 }
        if s.contains("靠左") || s.contains("keep left") { return 6 }
        if s.contains("靠右") || s.contains("keep right") { return 7 }
        if s.contains("左") || s.contains("left") { return 2 }
        if s.contains("右") || s.contains("right") { return 3 }
        return 1
    }
}
