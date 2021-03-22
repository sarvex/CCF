// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ds/json.h"
#include "enclave/app_interface.h"

#include <charconv>

namespace nobuiltins
{
  struct NodeSummary
  {
    ccf::QuoteFormat quote_format;
    std::vector<uint8_t> quote;
    std::vector<uint8_t> endorsements;

    kv::Consensus::View committed_view;
    kv::Consensus::SeqNo committed_seqno;
  };

  DECLARE_JSON_TYPE(NodeSummary)
  DECLARE_JSON_REQUIRED_FIELDS(
    NodeSummary, quote_format, quote, committed_view, committed_seqno)

  struct TransactionIDResponse
  {
    std::string transaction_id;
  };

  DECLARE_JSON_TYPE(TransactionIDResponse)
  DECLARE_JSON_REQUIRED_FIELDS(TransactionIDResponse, transaction_id)

  // SNIPPET: registry_inheritance
  class NoBuiltinsRegistry : public ccf::BaseEndpointRegistry
  {
  public:
    NoBuiltinsRegistry(ccfapp::AbstractNodeContext& context) :
      ccf::BaseEndpointRegistry("app", context)
    {
      auto node_summary = [this](ccf::EndpointContext& ctx) {
        ccf::ApiResult result;

        NodeSummary summary;

        {
          // SNIPPET_START: get_quote_api_v1
          ccf::QuoteInfo quote_info;
          result = get_quote_for_this_node_v1(ctx.tx, quote_info);
          if (result != ccf::ApiResult::OK)
          {
            ctx.rpc_ctx->set_error(
              HTTP_STATUS_INTERNAL_SERVER_ERROR,
              ccf::errors::InternalError,
              fmt::format(
                "Failed to get quote: {}", ccf::api_result_to_str(result)));
            return;
          }

          summary.quote_format = quote_info.format;
          summary.quote = quote_info.quote;
          summary.endorsements = quote_info.endorsements;
          // SNIPPET_END: get_quote_api_v1
        }

        {
          result = get_last_committed_txid_v1(
            summary.committed_view, summary.committed_seqno);
          if (result != ccf::ApiResult::OK)
          {
            ctx.rpc_ctx->set_error(
              HTTP_STATUS_INTERNAL_SERVER_ERROR,
              ccf::errors::InternalError,
              fmt::format(
                "Failed to get committed transaction: {}",
                ccf::api_result_to_str(result)));
            return;
          }
        }

        ctx.rpc_ctx->set_response_status(HTTP_STATUS_OK);
        ctx.rpc_ctx->set_response_body(nlohmann::json(summary).dump(2));
      };
      make_endpoint(
        "node_summary", HTTP_GET, node_summary, ccf::no_auth_required)
        .set_auto_schema<void, NodeSummary>()
        .install();

      auto openapi = [this](kv::Tx& tx, nlohmann::json&&) {
        nlohmann::json document;
        const auto result = generate_openapi_document_v1(
          tx,
          openapi_info.title,
          "A CCF sample demonstrating a minimal app, with no default endpoints",
          "0.0.1",
          document);

        if (result == ccf::ApiResult::OK)
        {
          return ccf::make_success(document);
        }
        else
        {
          return ccf::make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format(
              "Failed to generate OpenAPI: {}",
              ccf::api_result_to_str(result)));
        }
      };
      make_endpoint(
        "api", HTTP_GET, ccf::json_adapter(openapi), ccf::no_auth_required)
        .set_auto_schema<void, ccf::GetAPI::Out>()
        .install();

      auto get_commit = [this](auto&, nlohmann::json&&) {
        ccf::GetCommit::Out out;
        const auto result = get_last_committed_txid_v1(out.view, out.seqno);

        if (result == ccf::ApiResult::OK)
        {
          return ccf::make_success(out);
        }
        else
        {
          return ccf::make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format(
              "Failed to get committed transaction: {}",
              ccf::api_result_to_str(result)));
        }
      };
      make_command_endpoint(
        "commit",
        HTTP_GET,
        ccf::json_command_adapter(get_commit),
        ccf::no_auth_required)
        .set_execute_outside_consensus(
          ccf::endpoints::ExecuteOutsideConsensus::Locally)
        .set_auto_schema<void, ccf::GetCommit::Out>()
        .install();

      auto get_txid = [this](auto& ctx, nlohmann::json&&) {
        const auto query_string = ctx.rpc_ctx->get_request_query();
        const auto query_params = nonstd::split(query_string, "&");
        for (const auto& query_param : query_params)
        {
          const auto& [query_key, query_value] =
            nonstd::split_1(query_param, "=");
          if (query_key == "seqno")
          {
            kv::SeqNo seqno;
            const auto qv_begin = query_value.data();
            const auto qv_end = qv_begin + query_value.size();
            const auto [p, ec] = std::from_chars(qv_begin, qv_end, seqno);
            if (ec != std::errc() || p != qv_end)
            {
              return ccf::make_error(
                HTTP_STATUS_BAD_REQUEST,
                ccf::errors::InvalidQueryParameterValue,
                fmt::format(
                  "Query parameter '{}' cannot be parsed as a seqno",
                  query_value));
            }

            kv::Consensus::View view;
            const auto result = get_view_for_seqno_v1(seqno, view);
            if (result == ccf::ApiResult::OK)
            {
              ccf::TxID tx_id;
              tx_id.view = view;
              tx_id.seqno = seqno;

              TransactionIDResponse resp;
              resp.transaction_id = tx_id.to_str();

              return ccf::make_success(resp);
            }
            else
            {
              return ccf::make_error(
                HTTP_STATUS_INTERNAL_SERVER_ERROR,
                ccf::errors::InternalError,
                fmt::format(
                  "Unable to construct TxID: {}",
                  ccf::api_result_to_str(result)));
            }
          }
        }

        return ccf::make_error(
          HTTP_STATUS_BAD_REQUEST,
          ccf::errors::InvalidInput,
          fmt::format("Missing query parameter '{}'", "seqno"));
      };
      make_command_endpoint(
        "tx_id",
        HTTP_GET,
        ccf::json_command_adapter(get_txid),
        ccf::no_auth_required)
        .set_execute_outside_consensus(
          ccf::endpoints::ExecuteOutsideConsensus::Locally)
        .set_auto_schema<void, TransactionIDResponse>()
        .install();
    }
  };

  class NoBuiltinsFrontend : public ccf::UserRpcFrontend
  {
  private:
    NoBuiltinsRegistry nbr;

  public:
    NoBuiltinsFrontend(
      ccf::NetworkTables& network, ccfapp::AbstractNodeContext& context) :
      ccf::UserRpcFrontend(*network.tables, nbr),
      nbr(context)
    {}
  };
}

namespace ccfapp
{
  std::shared_ptr<ccf::UserRpcFrontend> get_rpc_handler(
    ccf::NetworkTables& nwt, ccfapp::AbstractNodeContext& context)
  {
    return std::make_shared<nobuiltins::NoBuiltinsFrontend>(nwt, context);
  }
}