#!/bin/zsh
# ios_profile.sh TEAM_ID BUNDLE_ID DEVICE_ID
# A development provisioning profile for the iPad app, with the two
# capabilities the game needs: extended virtual addressing (its 4 GB of
# guest memory) and the increased memory limit. App Store Connect's API
# cannot turn those on, so a stub app with them is built once with Xcode's
# automatic signing (your Apple ID signed in under Xcode > Settings >
# Accounts; a paid developer account); its profile is kept as
# $GTAIV_APPLE_BUILD/profiles/libertypromax.mobileprovision.
set -euo pipefail
here=${0:A:h:h}
team=$1 bundle=$2 device=$3
w=${GTAIV_APPLE_BUILD:-$here/out/apple}
stub=$w/profile-stub
rm -rf $stub && mkdir -p $stub/Stub $w/profiles
cat > $stub/project.yml <<YML
name: Stub
options: {deploymentTarget: {iOS: "17.0"}}
targets:
  Stub:
    type: application
    platform: iOS
    sources: [Stub]
    settings:
      PRODUCT_BUNDLE_IDENTIFIER: $bundle
      DEVELOPMENT_TEAM: $team
      CODE_SIGN_STYLE: Automatic
      CODE_SIGN_ENTITLEMENTS: Stub/Stub.entitlements
      GENERATE_INFOPLIST_FILE: YES
      TARGETED_DEVICE_FAMILY: "1,2"
YML
cp $here/port/ios/LibertyProMax.entitlements $stub/Stub/Stub.entitlements
cat > $stub/Stub/StubApp.swift <<'SWIFT'
import SwiftUI
@main struct StubApp: App { var body: some Scene { WindowGroup { Text("Liberty Pro Max") } } }
SWIFT
(cd $stub && xcodegen generate -q)
xcodebuild -project $stub/Stub.xcodeproj -scheme Stub -destination "id=$device" \
  -derivedDataPath $stub/dd -allowProvisioningUpdates build -quiet
cp "$(find $stub/dd -name embedded.mobileprovision | head -1)" $w/profiles/libertypromax.mobileprovision
echo "profile: $w/profiles/libertypromax.mobileprovision"
