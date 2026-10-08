import MapKit
import SwiftUI

struct ContentView: View {
    @StateObject private var nav = NavigationModel()
    @StateObject private var bluetooth = BLETransport()
    @State private var query = ""
    @State private var useDemo = false
    @State private var followPosition = true
    @State private var camera: MapCameraPosition = .automatic
    private let ticker = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    connectionCard
                    searchBox
                    mapCard
                    telemetryCard
                    HStack(spacing: 12) {
                        Button {
                            Task { await nav.startNavigation() }
                            useDemo = false
                        } label: {
                            Label("开始导航", systemImage: "location.north.line.fill")
                                .frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(nav.destination == nil || nav.navigating)
                        Button {
                            nav.stop()
                            useDemo = false
                            bluetooth.send(nav.snapshot)
                        } label: {
                            Label("结束", systemImage: "stop.fill")
                        }
                        .buttonStyle(.bordered)
                    }
                    Toggle(isOn: $useDemo) {
                        Label("模拟仪表数据", systemImage: "gauge.with.dots.needle.50percent")
                    }
                    .font(.subheadline)
                    .padding(14)
                    .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 14))
                    Text("iPhone 负责定位和路线计算。锁屏后的持续刷新取决于定位与蓝牙后台权限；须经真机骑行测试。请以交通标志和路况为准。")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding()
            }
            .background(Color(red: 0.045, green: 0.075, blue: 0.12))
            .navigationTitle("Passport Nav")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        followPosition = true
                        centerCamera()
                    } label: {
                        Image(systemName: "location.fill")
                    }
                }
            }
            .onReceive(ticker) { _ in
                bluetooth.send(useDemo ? .demonstration : nav.snapshot)
            }
            .onReceive(nav.$location) { _ in
                if followPosition { centerCamera() }
            }
            .onChange(of: useDemo) { _, active in
                bluetooth.send(active ? .demonstration : nav.snapshot)
            }
        }
        .tint(.mint)
    }

    private var connectionCard: some View {
        HStack(spacing: 12) {
            Image(systemName: bluetooth.ready ? "dot.radiowaves.left.and.right" : "antenna.radiowaves.left.and.right.slash")
                .font(.title2)
                .foregroundStyle(bluetooth.ready ? .mint : .orange)
            VStack(alignment: .leading, spacing: 3) {
                Text(bluetooth.state).font(.headline)
                Text("BLE · 20 字节导航协议").font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Button(bluetooth.ready ? "断开" : "连接") {
                if bluetooth.ready { bluetooth.disconnect() }
                else { bluetooth.scan() }
            }
            .buttonStyle(.bordered)
        }
        .padding(15)
        .background(.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 18))
    }

    private var searchBox: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("输入目的地，例如：枣庄站", text: $query)
                .submitLabel(.search)
                .onSubmit { Task { await nav.findDestination(query) } }
            Button("搜索") { Task { await nav.findDestination(query) } }
                .buttonStyle(.borderedProminent)
        }
        .padding(12)
        .background(.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 16))
    }

    private var mapCard: some View {
        Map(position: $camera) {
            if let route = nav.route {
                MapPolyline(route.polyline)
                    .stroke(.mint, style: StrokeStyle(lineWidth: 6, lineCap: .round))
            }
            if let destination = nav.destination {
                Marker(destination.name ?? "目的地",
                       coordinate: destination.placemark.coordinate)
                    .tint(.orange)
            }
        }
        .mapStyle(.standard(elevation: .flat))
        .frame(height: 300)
        .clipShape(RoundedRectangle(cornerRadius: 18))
        .overlay(alignment: .bottomLeading) {
            Text(nav.status)
                .font(.caption)
                .lineLimit(2)
                .padding(9)
                .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 9))
                .padding(10)
        }
    }

    private var telemetryCard: some View {
        let data = useDemo ? NavSnapshot.demonstration : nav.snapshot
        return VStack(alignment: .leading, spacing: 10) {
            HStack {
                Label(useDemo ? "演示数据" : "实时仪表", systemImage: "arrow.triangle.turn.up.right.diamond.fill")
                    .foregroundStyle(.mint)
                Spacer()
                Text(data.hasGPS ? "GPS" : "定位未就绪")
                    .font(.caption)
                    .foregroundStyle(data.hasGPS ? .mint : .orange)
            }
            Text(useDemo ? "右转进入下一条道路" : (nav.navigating ? nav.stepText : "等待开始导航"))
                .font(.title3.bold())
                .lineLimit(2)
            HStack(alignment: .firstTextBaseline) {
                Text("\(data.turnMeters) m")
                    .font(.system(size: 40, weight: .bold, design: .rounded))
                Spacer()
                VStack(alignment: .trailing) {
                    Text("\(data.speedTenthsKmh / 10)")
                        .font(.system(size: 30, weight: .semibold, design: .rounded))
                    Text("km/h").font(.caption).foregroundStyle(.secondary)
                }
            }
            HStack {
                Label(String(format: "%.2f km", Double(data.remainingTensMeters) / 100),
                      systemImage: "point.topleft.down.to.point.bottomright.curvepath")
                Spacer()
                Label("\(data.remainingSeconds / 60) 分钟", systemImage: "clock")
            }
            .font(.subheadline)
            .foregroundStyle(.secondary)
        }
        .padding(16)
        .background(.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 18))
    }

    private func centerCamera() {
        guard let here = nav.location else { return }
        camera = .region(MKCoordinateRegion(center: here.coordinate,
                                           latitudinalMeters: nav.navigating ? 2500 : 8000,
                                           longitudinalMeters: nav.navigating ? 2500 : 8000))
    }
}
