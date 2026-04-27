#pragma once

// FIX 4.2 Application Layer Messages

#include "nexusfix/messages/fix42/new_order_single.hpp"
#include "nexusfix/messages/fix42/order_cancel_request.hpp"
#include "nexusfix/messages/fix42/execution_report.hpp"

namespace nfx::fix42 {

// All FIX 4.2 application message types:
// - NewOrderSingle (D) - new_order_single.hpp
// - OrderCancelRequest (F) - order_cancel_request.hpp
// - ExecutionReport (8) - execution_report.hpp
//
// FIX 4.2 differences from FIX 4.4:
// - ExecTransType (Tag 20) is required in ExecutionReport
// - LeavesQty (Tag 151) is optional in ExecutionReport
// - HandlInst (Tag 21) is required in NewOrderSingle
//
// Session messages (Logon, Logout, Heartbeat, etc.) reuse the fix44
// namespace with begin_string set to "FIX.4.2".

} // namespace nfx::fix42
