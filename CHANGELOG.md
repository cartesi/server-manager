# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.8.2] - 2023-08-21
### Changed
- Updated server-manager version to v0.8.2
- Updated machine-emulator base image to v0.15.2

## [0.8.1] - 2023-08-17
### Changed
- Updated server-manager version to v0.8.1
- Updated machine-emulator base image to v0.15.1

## [0.8.0] - 2023-08-16
### Added
- Added support for arm64 docker images using depot.dev
- Added Pull-Requests and Issues templates

### Changed
- Changed logic to reject input/query when payload is too long
- Updated license/copyright notice in all code
- Updated grpc-interfaces to v0.14.0
- Updated server-manager version to v0.8.0
- Updated machine-emulator base image to v0.15.0

## [0.7.0] - 2023-05-12
### Changed
- Updated server-manager to match GRPC interfaces changes
- The source code was moved to the new repository
- The creation of test machines was extracted from the test-server-manager to a lua script
- The license changed to Apache license 2.0

[Unreleased]: https://github.com/cartesi/server-manager/compare/v0.8.2...HEAD
[0.8.2]: https://github.com/cartesi/server-manager/releases/tag/v0.8.2
[0.8.1]: https://github.com/cartesi/server-manager/releases/tag/v0.8.1
[0.8.0]: https://github.com/cartesi/server-manager/releases/tag/v0.8.0
[0.7.0]: https://github.com/cartesi/server-manager/releases/tag/v0.7.0
