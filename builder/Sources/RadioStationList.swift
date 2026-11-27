import Foundation

struct RadioStationList {
  static let basePath = "update/pc/audio/config/GAME.DAT16"

  // Lowercase Jenkins hashes of RADIO_STATION_RAMJAMFM, RADIO_STATION_MEDITATION
  // (Self-Actualization FM), and RADIO_STATION_VCFM (Vice City FM).
  static let episodeStations: Set<UInt32> = [0xfa50fd35, 0xd7ff8ab2, 0xecf2350a]

  let stationIDs: [UInt32]
  private let countOffset: Int
  private let sizeOffset: Int

  /// Read the station list from the DAT16 object index.
  /// https://github.com/Parik27/GTAAudioMetadataTool/blob/master/src/common/MetadataMgr.hh
  init(_ data: Data) throws {
    let invalid = Failure("Radio Restoration has an invalid GTA IV station list.")
    let objectsStart = 8
    guard data.word(at: 0) == 16, let objectsSize = data.word(at: 4) else { throw invalid }
    let archiveIndex = objectsStart + Int(objectsSize)
    guard let archiveSize = data.word(at: archiveIndex), archiveSize >= 4 else { throw invalid }
    let objectIndex = archiveIndex + 4 + Int(archiveSize)
    guard let objectCount = data.word(at: objectIndex),
          objectCount <= 10_000, objectIndex + 8 <= data.count else { throw invalid }

    let name = Data("GTA4_RADIO_STATIONS_01".utf8)
    var cursor = objectIndex + 8
    for _ in 0..<objectCount {
      guard cursor < data.count else { throw invalid }
      let start = cursor + 1
      let end = start + Int(data[cursor])
      guard let offset = data.word(at: end), let size = data.word(at: end + 4) else { throw invalid }
      if data[start..<end].elementsEqual(name) {
        let record = objectsStart + Int(offset)
        let headerSize = 10
        let countOffset = record + headerSize
        guard size > headerSize, record + Int(size) <= archiveIndex,
              data[record] == 10 else { throw invalid }
        let count = Int(data[countOffset])
        guard count > 0, Int(size) == headerSize + 1 + 4 * count else { throw invalid }
        var stations: [UInt32] = []
        for index in 0..<count {
          guard let station = data.word(at: countOffset + 1 + 4 * index) else { throw invalid }
          stations.append(station)
        }
        stationIDs = stations
        self.countOffset = countOffset
        sizeOffset = end + 4
        return
      }
      cursor = end + 8
    }
    throw invalid
  }

  static func removeEpisodeStations(from data: inout Data) throws {
    let list = try RadioStationList(data)
    let stations = list.stationIDs.filter { !episodeStations.contains($0) }
    guard !stations.isEmpty, list.stationIDs.count - stations.count == episodeStations.count,
          episodeStations.isSubset(of: list.stationIDs) else {
      throw Failure("Radio Restoration has unexpected episode stations in its GTA IV station list.")
    }
    let first = list.countOffset + 1
    data[list.countOffset] = UInt8(stations.count)
    for (index, station) in stations.enumerated() {
      data.setWord(station, at: first + 4 * index)
    }
    // Zero unused bytes and shorten the index entry to keep other objects at their original offsets.
    let unused = (first + 4 * stations.count)..<(first + 4 * list.stationIDs.count)
    data.resetBytes(in: unused)
    data.setWord(UInt32(11 + 4 * stations.count), at: list.sizeOffset)
  }
}

private extension Data {
  func word(at offset: Int) -> UInt32? {
    guard offset >= 0, offset <= count - 4 else { return nil }
    return withUnsafeBytes {
      UInt32(littleEndian: $0.loadUnaligned(fromByteOffset: offset, as: UInt32.self))
    }
  }

  mutating func setWord(_ value: UInt32, at offset: Int) {
    withUnsafeMutableBytes { (buffer: UnsafeMutableRawBufferPointer) in
      buffer.storeBytes(of: value.littleEndian, toByteOffset: offset, as: UInt32.self)
    }
  }
}
