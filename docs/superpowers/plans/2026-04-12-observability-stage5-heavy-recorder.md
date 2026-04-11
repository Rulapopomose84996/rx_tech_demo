# Observability Stage 5 Heavy Recorder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Treat `RawFrameRecorder` as a heavy debug recorder with explicit start/stop/failure events and clear summary labeling, without changing its core buffering/retention mechanics.

**Architecture:** Keep `RawFrameRecorder` itself focused on raw frame buffering and file rotation. Emit lifecycle events from `ReceiveRunner`, carry a `raw_record_role` marker through `CaptureSummary`, and surface that role in `summary.json`, `summary.txt`, and the existing human summary. Validation stays local to WSL fallback and fake integration.

**Tech Stack:** C++17, CMake 3.16, Ninja, existing `rx_receiver_core` / fake integration harness, WSL fallback validation

---
