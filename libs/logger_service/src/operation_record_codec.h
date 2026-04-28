/*
 * Copyright (c) 2026 CBinary
 * Author: CBinary
 * File: operation_record_codec.h
 * Brief: Defines internal JSON Lines codec helpers for operation records.
 */

#ifndef LIVE_STREAM_LOGGER_SERVICE_OPERATION_RECORD_CODEC_H_
#define LIVE_STREAM_LOGGER_SERVICE_OPERATION_RECORD_CODEC_H_

#include "logger_service.h"

#include <string>

namespace live_stream {

std::string EncodeOperationRecord(const OperationRecord& record);
bool DecodeOperationRecord(const std::string& line, OperationRecord* record);
bool OperationActionFromString(const std::string& value, OperationAction* action);
bool OperationResultFromString(const std::string& value, OperationResult* result);

}  // namespace live_stream

#endif  // LIVE_STREAM_LOGGER_SERVICE_OPERATION_RECORD_CODEC_H_
