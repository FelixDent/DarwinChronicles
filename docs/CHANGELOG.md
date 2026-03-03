# Changelog

All notable changes to Darwin Chronicles will be documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- Project scaffolding: CMake build system with C++20, FetchContent dependencies
- Six module static libraries: environment, world, organisms, evolution, simulation, rendering
- Stub headers and source files for all planned components
- Catch2 test harness with placeholder tests for each module
- Google Benchmark harness for performance testing
- Example programs: earth_like, mars_like
- Makefile convenience targets (build, test, bench, run, format, clean)
- Compiler warning target (`darwin_warnings`) with aggressive diagnostics
- PixelOdyssey terrain tilesets (16x16, 32x32, 48x48) covering 12 biome themes
- Architecture Decision Records (ADRs 0001–0006)
- Project documentation: ARCHITECTURE.md, CHANGELOG.md, docs README
- System design documents: planetary model, world generation, nutrient regeneration, neural evolution, sprite generation
