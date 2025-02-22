
#include <graphene/crosschain/crosschain_interface_ltc.hpp>
#include <fc/network/ip.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>
#include <iostream>
#include <graphene/crosschain_privatekey_management/private_key.hpp>
#include <fc/thread/scoped_lock.hpp>
#include <fc/asio.hpp>
#include <fc/network/resolve.hpp>

#include "../common.hpp"

namespace graphene {
	namespace crosschain {


		void crosschain_interface_ltc::initialize_config(fc::variant_object &json_config)
		{
			_config = json_config;
			_rpc_method = "POST";
			_rpc_url = "http://";
			_rpc_url = _rpc_url + _config["ip"].as_string() + ":" + std::string(_config["port"].as_string())+"/api";
		}
		bool crosschain_interface_ltc::valid_config()
		{
			if (_config.contains("ip") && _config.contains("port"))
				return true;
			return false;
		}
		bool crosschain_interface_ltc::unlock_wallet(std::string wallet_name, std::string wallet_passprase, uint32_t duration)
		{
			return false;
		}

		bool crosschain_interface_ltc::open_wallet(std::string wallet_name)
		{
			return false;
		}

		void crosschain_interface_ltc::close_wallet()
		{
			
		}

		std::vector<std::string> crosschain_interface_ltc::wallet_list()
		{
			return std::vector<std::string>();
		}

		bool graphene::crosschain::crosschain_interface_ltc::create_wallet(std::string wallet_name, std::string wallet_passprase)
		{
			return false;
		}

		std::string crosschain_interface_ltc::create_normal_account(std::string account_name, const fc::optional<fc::ecc::private_key> key/*=fc::optional<fc::ecc::private_key>()*/)
		{
			auto ptr = graphene::privatekey_management::crosschain_management::get_instance().get_crosschain_prk(chain_type);
			if (ptr == nullptr)
				return "";
			ptr->generate(key);

			//std::ostringstream req_body;
			//req_body << "{ \"jsonrpc\": \"2.0\", \
            //    \"id\" : \"45\", \
			//	\"method\" : \"Zchain.Addr.importAddr\" ,\
			//	\"params\" : {\"chainId\":\"ltc\" ,\"addr\": \"" << ptr->get_address() << "\"}}";
			//std::cout << req_body.str() << std::endl;
			//fc::http::connection_sync conn;
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			//auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			////std::cout << std::string(response.body.begin(), response.body.end()) << std::endl;
			return ptr->get_wif_key();
		}
		
		std::map<std::string,std::string> crosschain_interface_ltc::create_multi_sig_account(std::string account_name, std::vector<std::string> addresses, uint32_t nrequired)
		{
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Multisig.Create\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"amount\": " << nrequired <<",\""<<"addrs\":[";

			for (int i = 0; i < addresses.size(); ++i)
			{
				req_body << "\"" << addresses[i] << "\"";
				if (i < addresses.size() - 1)
				{
					req_body << ",";
				}

			}
			req_body << "]}}";
			fc::http::connection_sync conn;
			connect_midware(conn);
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			std::cout << response.status << std::endl;
			if (response.status == fc::http::reply::OK)
			{
				std::cout << std::string(response.body.begin(), response.body.end()) << std::endl;
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
				auto ret = resp.get_object();
				if (ret.contains("result"))
				{
					std::map<std::string, std::string> map;
					auto result = ret["result"].get_object();
					FC_ASSERT(result.contains("address"));
					map["address"] = result["address"].as_string();
					FC_ASSERT(result.contains("redeemScript"));
					map["redeemScript"] = result["redeemScript"].as_string();
					return map;
				}
			}
	
		    FC_THROW(account_name) ;
			//return std::string();
		}

		std::vector<graphene::crosschain::hd_trx> crosschain_interface_ltc::deposit_transaction_query(std::string user_account, uint32_t from_block, uint32_t limit)
		{
			return std::vector<graphene::crosschain::hd_trx>();
		}

		fc::variant_object crosschain_interface_ltc::transaction_query(vector<std::string>& trx_ids)
		{
			try {
				std::ostringstream req_body;
				req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Trans.queryTransBatch\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"trxids\": [";
				for (int i = 0; i < trx_ids.size(); ++i)
				{
					req_body << "\"" << trx_ids[i] << "\"";
					if (i < trx_ids.size() - 1)
					{
						req_body << ",";
					}
				}
				req_body << "]}}";
				std::cout << req_body.str() << std::endl;
				for (const auto& q : _trxs_queue)
				{
					if (get<0>(q) == req_body.str())
						return get<1>(q);
				}
				fc::http::connection_sync conn;
				connect_midware(conn);
				//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
				auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
				if (response.status == fc::http::reply::OK)
				{
					auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end())).get_object();

					FC_ASSERT(resp.contains("result"));
					FC_ASSERT(resp["result"].get_object().contains("data"));
					const auto& result = resp["result"].get_object()["data"].get_object();
					_trxs_queue.push_back(std::tuple<std::string, fc::variant_object>(req_body.str(), result));
					return result;
				}
				else
				{
					FC_ASSERT(false);
				}
			}FC_CAPTURE_AND_RETHROW((trx_ids));

		}

		fc::variant_object crosschain_interface_ltc::transaction_query(std::string trx_id)
		{
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Trans.queryTrans\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"trxid\": \"" << trx_id << "\"}}";
			std::cout << req_body.str() << std::endl;
			for (const auto& q : _trxs_queue)
			{
				if (get<0>(q) == req_body.str())
					return get<1>(q);
			}
			fc::http::connection_sync conn;
			connect_midware(conn);
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end())).get_object();
				
				FC_ASSERT(resp.contains("result"));
				FC_ASSERT(resp["result"].get_object().contains("data"));
				const auto& result = resp["result"].get_object()["data"].get_object();
				_trxs_queue.push_back(std::tuple<std::string, fc::variant_object>(req_body.str(), result));
				return result;
			}
			else
				FC_THROW(trx_id);
		}

		fc::variant_object crosschain_interface_ltc::transfer(const std::string &from_account, const std::string &to_account, uint64_t amount, const std::string &symbol, const std::string &memo, bool broadcast /*= true*/)
		{
			return fc::variant_object();
		}


		fc::variant_object crosschain_interface_ltc::create_multisig_transaction(const std::string &from_account, const std::map<std::string, std::string> dest_info, const std::string &symbol, const std::string &memo)
		{
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Trans.createTrx\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"from_addr\": \"" << from_account << "\",\"dest_info\":{";// << to_account << "\",\"amount\":" << amount << "}}";
			for (auto iter = dest_info.begin(); iter != dest_info.end(); ++iter)
			{
				if (iter != dest_info.begin())
					req_body << ",";
				req_body << "\"" << iter->first << "\":" << iter->second;
			}
			req_body << "}}}";
			std::cout << req_body.str() << std::endl;
			fc::http::connection_sync conn;
			connect_midware(conn);
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto str = std::string(response.body.begin(), response.body.end());
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
				auto ret = resp.get_object()["result"].get_object();
				FC_ASSERT(ret.contains("data"));
				return ret["data"].get_object();
			}
			else
				FC_THROW("TODO");
			return fc::variant_object();

		}

		fc::variant_object crosschain_interface_ltc::create_multisig_transaction(std::string &from_account, std::string &to_account, const std::string& amount, std::string &symbol, std::string &memo, bool broadcast)
		{
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Trans.createTrx\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"from_addr\": \"" << from_account << "\",\"to_addr\":\""<<to_account <<"\",\"amount\":" <<amount <<"}}";
			std::cout << req_body.str() << std::endl;
			fc::http::connection_sync conn;
			connect_midware(conn);
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto str = std::string(response.body.begin(), response.body.end());
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
				auto ret =resp.get_object()["result"].get_object();
				FC_ASSERT(ret.contains("data"));
				return ret["data"].get_object();
			}
			else
				FC_THROW("TODO");
			return fc::variant_object();
		}

		std::string crosschain_interface_ltc::sign_multisig_transaction(fc::variant_object trx, graphene::privatekey_management::crosschain_privatekey_base*& sign_key,const std::string& redeemScript,bool broadcast /*= true*/)
		{

			try {
				FC_ASSERT(trx.contains("hex"));
				return sign_key->mutisign_trx(redeemScript, trx);
			}
			FC_CAPTURE_AND_RETHROW((trx)(redeemScript));
			/*
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Trans.Sign\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"addr\": \"" << sign_account << "\",\"trx_hex\":\"" << trx["hex"].as_string() << "\","<<"\"redeemScript"<<"\":"<<"\""<<redeemScript<<"\"}}";
			std::cout << req_body.str() << std::endl;
			fc::http::connection_sync conn;
			conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end())).get_object();
				std::cout << std::string(response.body.begin(), response.body.end()) << std::endl;
				FC_ASSERT(resp.contains("result"));
				auto ret = resp["result"].get_object();
				FC_ASSERT(ret.contains("data"));
				return ret["data"].get_object()["hex"].as_string();
			}
			else
				FC_THROW("TODO");
			return std::string();
			*/
		}

		fc::variant_object crosschain_interface_ltc::merge_multisig_transaction(fc::variant_object &trx, std::vector<std::string> signatures)
		{
			graphene::privatekey_management::ltc_privatekey btk;
			return btk.combine_trxs(signatures);
			/*std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
				\"id\" : \"45\", \
				\"method\" : \"Zchain.Trans.CombineTrx\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"transactions\": [";
			for (auto itr = signatures.begin(); itr != signatures.end(); ++itr)
			{
				req_body << "\"" << *itr << "\"";
				if (itr != signatures.end() - 1)
				{
					req_body << ",";
				}
			}
			req_body << "]}}";
			fc::http::connection_sync conn;
			conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end())).get_object();
				FC_ASSERT(resp.contains("result"));
				auto ret = resp["result"].get_object();
				FC_ASSERT(ret.contains("data"));
				return ret["data"].get_object();
			}
			else
				FC_THROW(std::string(response.body.begin(), response.body.end()));
			return fc::variant_object();*/
		}

		bool crosschain_interface_ltc::validate_link_trx(const hd_trx &trx)
		{
			auto tx = transaction_query(trx.trx_id);
			FC_ASSERT(tx.contains("txid"));
			FC_ASSERT(tx.contains("vin"));
			FC_ASSERT(tx.contains("vout"));
			FC_ASSERT(trx.trx_id == tx["txid"].as_string());
			//get vin
			bool checkfrom = false;
			auto vins = tx["vin"].get_array();
			std::vector<std::string> vin_trxs;
			for (auto vin : vins)
			{
				auto sztx = vin.get_object()["txid"].as_string();
				FC_ASSERT(vin.get_object().contains("txid"));
				auto itr = std::find(vin_trxs.begin(), vin_trxs.end(), sztx);
				if (itr == vin_trxs.end())
					vin_trxs.push_back(sztx);
			}
			const auto& vin_ret = transaction_query(vin_trxs);
			FC_ASSERT(vin_ret.size() == vins.size());
			for (auto vin : vins)
			{
				try {
					auto vin_tx = vin_ret[vin.get_object()["txid"].as_string()].get_object();
					FC_ASSERT(vin_tx.contains("vout"));
					auto vouts = vin_tx["vout"].get_array();
					for (auto vout : vouts)
					{
						FC_ASSERT(vout.get_object().contains("scriptPubKey"));
						FC_ASSERT(vout.get_object().contains("value"));
						auto scriptPubKey = vout.get_object()["scriptPubKey"].get_object();
						FC_ASSERT(scriptPubKey.contains("addresses"));

						auto vout_address = scriptPubKey["addresses"].get_array();
						if (vout_address.size() != 1) {
							continue;
						}
						if (vout_address[0].as_string() == trx.from_account) {
							checkfrom = true;
						}
					}
				}
				catch (...) {
					continue;
				}
			}
			//check vout
			bool checkto = false;
			auto vouts = tx["vout"].get_array();
			for (auto vout : vouts)
			{
				try {
					FC_ASSERT(vout.get_object().contains("scriptPubKey"));
					FC_ASSERT(vout.get_object().contains("value"));
					auto scriptPubKey = vout.get_object()["scriptPubKey"].get_object();
					FC_ASSERT(scriptPubKey.contains("addresses"));

					auto vout_address = scriptPubKey["addresses"].get_array();
					if (vout_address.size() != 1) {
						continue;
					}
					if (vout_address[0].as_string() == trx.to_account) {
						char temp[1024];
						std::sprintf(temp, "%.8f", vout.get_object()["value"].as_double());
						std::string source_amount = graphene::utilities::remove_zero_for_str_amount(temp);
						FC_ASSERT(source_amount == trx.amount);
						checkto = true;
					}
				}
				catch (...) {
					continue;
				}

			}

			/*
			FC_ASSERT(trx->from_account == );
			FC_ASSERT(trx->to_account == );
			FC_ASSERT(trx->amount == );
			FC_ASSERT(trx->block_num == );
			*/
			if (_trxs_queue.size() > 30)
				_trxs_queue.clear();
			return (checkfrom && checkto);
		}

		bool crosschain_interface_ltc::validate_link_trx(const std::vector<hd_trx> &trx)
		{
			return false;
		}

		bool crosschain_interface_ltc::validate_other_trx(const fc::variant_object &trx)
		{
			return true;
		}
		bool crosschain_interface_ltc::validate_transaction( const std::string& addr,const std::string& redeemscript,const std::string& sig)
		{
			try {
				graphene::privatekey_management::ltc_privatekey ltk;
				return ltk.validate_transaction(addr,redeemscript,sig);
			}FC_CAPTURE_AND_RETHROW((addr)(redeemscript)(sig));
		}


		bool crosschain_interface_ltc::validate_address(const std::string& addr)
		{

			graphene::privatekey_management::ltc_privatekey ltk;
			return ltk.validate_address(addr);
			/*
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Address.validate\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"addr\": " << "\"" << addr << "\"}}";
			fc::http::connection_sync conn;
			conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
				auto ret = resp.get_object();
				if (ret.contains("result"))
				{
					auto result = ret["result"].get_object();
					return result["valid"].as_bool();
				}
				else
				{
					return false;
				}

			}
			else
				FC_THROW(addr);
				*/
		}
		bool crosschain_interface_ltc::validate_signature(const std::string &account, const std::string &content, const std::string &signature)
		{
			graphene::privatekey_management::ltc_privatekey ltk;
			return ltk.verify_message(account, content, signature);

			/*std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Crypt.VerifyMessage\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"addr\": " << "\"" << account << "\"," << "\"message\":" << "\""  \
				<< content << "\"," << "\""<<"signature" <<"\":\""<<signature <<"\""<<"}}";
			fc::http::connection_sync conn;
			conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
				auto ret = resp.get_object();
				if (ret.contains("result"))
				{
					auto result = ret["result"].get_object();
					return result["data"].as_bool();
				}
				else
				{
					return false;
				}
				
			}
			else
				FC_THROW(signature);
				*/
		}

		bool crosschain_interface_ltc::create_signature(graphene::privatekey_management::crosschain_privatekey_base*& sign_key, const std::string &content, std::string &signature)
		{

			signature = "";
			signature = sign_key->sign_message(content);
			if (signature == "")
				return false;
			return true;

			/*
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Crypt.Sign\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"addr\": " <<"\""<<account<<"\"," <<"\"message\":"<<"\""<<content<<"\"" <<"}}";
			fc::http::connection_sync conn;
			conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
			    auto result=resp.get_object();
				if (result.contains("result"))
				{
					signature = result["result"].get_object()["data"].as_string();
					return true;
				}
				return false;
			}
			else
				FC_THROW(signature);
			*/
		}
		std::string crosschain_interface_ltc::get_from_address(const fc::variant_object& trx)
		{
			try {
				auto tx = trx["trx"].get_object();
				for (auto vin : tx["vin"].get_array())
				{
					auto index = vin.get_object()["vout"].as_uint64();
					auto from_trx_id = vin.get_object()["txid"].as_string();
					auto from_trx = transaction_query(from_trx_id);
					return from_trx["vout"].get_array()[index].get_object()["scriptPubKey"].get_object()["addresses"].get_array()[0].as_string();
				}
				return std::string();
			}FC_CAPTURE_AND_RETHROW((trx));
		}

		crosschain_trx crosschain_interface_ltc::turn_trxs(const fc::variant_object & trx)
		{
			hd_trx hdtx;
			crosschain_trx hdtxs;
			try {
				auto tx = trx["trx"].get_object();
				hdtx.asset_symbol = chain_type;
				hdtx.trx_id = tx["hash"].as_string();
				const std::string to_addr = tx["vout"].get_array()[0].get_object()["scriptPubKey"].get_object()["addresses"].get_array()[0].as_string();

				double total_vin = 0.0;
				double total_vout = 0.0;
				// need to get the fee 
				std::vector<std::string> vec;
				auto vins = tx["vin"].get_array();
				for (auto vin : vins)
				{
					auto index = vin.get_object()["vout"].as_uint64();
					auto from_trx_id = vin.get_object()["txid"].as_string();
					vec.push_back(from_trx_id);
				}
				FC_ASSERT(vec.size() == vins.size());
				auto vins_ret = transaction_query(vec);
				for (auto vin : vins)
				{
					auto index = vin.get_object()["vout"].as_uint64();
					auto from_trx_id = vin.get_object()["txid"].as_string();
					auto from_trx = vins_ret[from_trx_id].get_object();
					const std::string from_addr = from_trx["vout"].get_array()[index].get_object()["scriptPubKey"].get_object()["addresses"].get_array()[0].as_string();
					hdtx.from_account = from_addr;
					total_vin += from_trx["vout"].get_array()[index].get_object()["value"].as_double();
				}
				for (auto vouts : tx["vout"].get_array())
				{
					auto addrs = vouts.get_object()["scriptPubKey"].get_object()["addresses"].get_array();
					for (auto addr : addrs)
					{

						hdtx.to_account = addr.as_string();
						auto amount = vouts.get_object()["value"].as_double();
						if (addr.as_string() == hdtx.from_account)
						{
							total_vout += amount;
							continue;
						}
						char temp[1024];
						std::sprintf(temp, "%.8f", amount);

						hdtx.amount = graphene::utilities::remove_zero_for_str_amount(temp);
						total_vout += amount;
						hdtxs.trxs[hdtx.to_account] = hdtx;

					}

				}
				hdtxs.fee = total_vin - total_vout;
			}
			FC_CAPTURE_AND_RETHROW((trx));
			return hdtxs;
		}

		graphene::crosschain::hd_trx crosschain_interface_ltc::turn_trx(const fc::variant_object & trx)
		{
			hd_trx hdtx;
			try {
				hdtx.asset_symbol = chain_type;
				hdtx.trx_id = trx["txid"].as_string();

				hdtx.from_account = trx["from_account"].as_string();
				hdtx.to_account = trx["to_account"].as_string();
				hdtx.block_num = trx["blockNum"].as_uint64();
				char temp[1024];
				std::sprintf(temp, "%.8f", trx["amount"].as_double());
				hdtx.amount = graphene::utilities::remove_zero_for_str_amount(temp);
				
			}
			FC_CAPTURE_AND_RETHROW((trx));
			return hdtx;
		}

		void crosschain_interface_ltc::broadcast_transaction(const fc::variant_object &trx)
		{
			try {
				std::ostringstream req_body;
				req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Trans.broadcastTrx\" ,\
				\"params\" : {\"chainId\":\"ltc\" ,\"trx\": " << "\"" << trx["hex"].as_string() <<"\"" << "}}";
				fc::http::connection_sync conn;
				connect_midware(conn);
				//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
				auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
				if (response.status == fc::http::reply::OK)
				{
					auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
					auto result = resp.get_object();
					if (result.contains("result"))
					{
						auto hex = result["result"].get_object()["data"].as_string();
					}
				}
			}FC_CAPTURE_AND_LOG((trx));
		}

		std::vector<fc::variant_object> crosschain_interface_ltc::query_account_balance(const std::string &account)
		{
			return std::vector<fc::variant_object>();
		}

		std::vector<fc::variant_object> crosschain_interface_ltc::transaction_history(std::string symbol,const std::string &user_account, uint32_t start_block, uint32_t limit, uint32_t& end_block_num)
		{
			std::vector<fc::variant_object> return_value;
			std::string local_symbol = "ltc";
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Transaction.Deposit.History\" ,\
				\"params\" : {\"chainId\":\""<< local_symbol<<"\",\"account\": \"\" ,\"limit\": 0 ,\"blockNum\": "  << start_block << "}}";
			fc::http::connection_sync conn;
			connect_midware(conn);
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
				//std::cout << std::string(response.body.begin(), response.body.end());
				auto result = resp.get_object();
				if (result.contains("result"))
				{
					end_block_num = result["result"].get_object()["blockNum"].as_uint64();
					for (auto one_data : result["result"].get_object()["data"].get_array())
					{
						//std::cout << one_data.get_object()["txid"].as_string();
						return_value.push_back(one_data.get_object());
					}
				}
			}
			std::ostringstream req_body1;
			req_body1 << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Transaction.Withdraw.History\" ,\
				\"params\" : {\"chainId\":\"" << local_symbol << "\",\"account\": \"\" ,\"limit\": 0 ,\"blockNum\": " << start_block << "}}";
			fc::http::connection_sync conn1;

			connect_midware(conn1);
			//conn1.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response1 = conn1.request(_rpc_method, _rpc_url, req_body1.str(), _rpc_headers);
			if (response1.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response1.body.begin(), response1.body.end()));
				//std::cout << std::string(response.body.begin(), response.body.end());
				auto result = resp.get_object();
				if (result.contains("result"))
				{
					end_block_num = std::max(uint32_t(result["result"].get_object()["blockNum"].as_uint64()),end_block_num);
					for (auto one_data : result["result"].get_object()["data"].get_array())
					{
						//std::cout << one_data.get_object()["txid"].as_string();
						return_value.push_back(one_data.get_object());
					}
				}
			}

			return return_value;
		}

		std::string crosschain_interface_ltc::export_private_key(std::string &account, std::string &encrypt_passprase)
		{
			std::ostringstream req_body;
			req_body << "{ \"id\": 1, \"method\": \"dumpprivkey\", \"params\": [\""
				<< account << "\"]}";
			fc::http::connection_sync conn;

			connect_midware(conn);
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);
			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end())).as<fc::mutable_variant_object>();
				return resp["result"].as_string();
			}
			else
				FC_THROW(account);
		}

		std::string crosschain_interface_ltc::import_private_key(std::string &account, std::string &encrypt_passprase)
		{
			return std::string();
		}

		std::string crosschain_interface_ltc::backup_wallet(std::string &wallet_name, std::string &encrypt_passprase)
		{
			return std::string();
		}
		std::vector<fc::ip::endpoint> abstract_crosschain_interface::midware_eps;
		bool abstract_crosschain_interface::b_get_eps_from_service = true;
		std::vector<fc::ip::endpoint> abstract_crosschain_interface::midware_eps_backup;
		std::map<fc::ip::endpoint, std::pair<int, int>> abstract_crosschain_interface::connect_counts;
		std::mutex abstract_crosschain_interface::eps_lock;
		void abstract_crosschain_interface::set_midwares(const std::vector<fc::ip::endpoint>& eps)
		{
			std::lock_guard<std::mutex> lock(eps_lock);
			FC_ASSERT(eps.size() > 0);
			midware_eps = eps;
		}
		void abstract_crosschain_interface::set_midwares_backup(const std::vector<fc::ip::endpoint>& eps)
		{
			std::lock_guard<std::mutex> lock(eps_lock);
			FC_ASSERT(eps.size() > 0);
			midware_eps_backup = eps;
		}
		std::vector<fc::ip::endpoint> abstract_crosschain_interface::get_midware_eps()
		{
			std::lock_guard<std::mutex> lock(eps_lock);
			std::vector<fc::ip::endpoint> res= midware_eps;
			return res;
		}
// 		std::vector<fc::ip::endpoint> abstract_crosschain_interface::get_midware_from_server()
// 		{
// 			try {
// 				std::vector<fc::ip::endpoint> midware_seeds;
// 				boost::asio::ip::tcp::resolver resolver(fc::asio::default_io_service());
// 				boost::asio::ip::tcp::resolver::query queryEndpoints("1000896736104835.cn-hongkong.fc.aliyuncs.com", "80");
// 				boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(queryEndpoints);
// 				;
// 				for (boost::asio::ip::tcp::resolver::iterator iterNull;
// 					endpoint_iterator != iterNull;
// 					endpoint_iterator++)
// 				{
// 					try {
// 						midware_seeds.push_back(fc::ip::endpoint::from_string(endpoint_iterator->endpoint().address().to_string() + ":" + std::to_string(endpoint_iterator->endpoint().port())));
// 					}
// 					catch (...)
// 					{
// 					}
// 				}
// 				for (auto& seed_ep : midware_seeds)
// 				{
// 					try {
// 						fc::http::connection_sync conn;
// 						conn.connect_to(seed_ep);
// 						//auto res = conn.parse_reply();
// 						auto response = conn.request("GET", "http://1000896736104835.cn-hongkong.fc.aliyuncs.com/2016-08-15/proxy/query_XWC_middleware_endpoint/query_middleware_endpoint/", "");
// 						if (response.status == fc::http::reply::OK)
// 						{
// 							auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
// 							std::cout << std::string(response.body.begin(), response.body.end());
// 							auto result = resp.get_object();
// 							if (result.contains("result"))
// 							{
// 
// 								vector<fc::ip::endpoint> midware_sers;
// 								auto eps = result["result"].get_array();
// 								for (auto ep : eps)
// 								{
// 									auto str = ep["ip"].get_string() + ":" + std::to_string(ep["port"].as_int64());
// 									midware_sers.push_back(fc::ip::endpoint::from_string(str));
// 								}
// 								srand(time(NULL));
// 								int idx = rand() % midware_sers.size();
// 								if (idx != 0)
// 									swap(midware_sers[0], midware_sers[idx]);
// 								return midware_sers;
// 							}
// 						}
// 					}
// 					catch (...)
// 					{
// 					}
// 				}
// 			}
// 			catch (...)
// 			{
// 
// 			}
// 			return std::vector<fc::ip::endpoint>();
// 		}
		void abstract_crosschain_interface::connect_midware(fc::http::connection_sync& con)
		{
			
			vector<int> counts;
			int ep_idx = -1;
			while(ep_idx==-1)
			{
				bool caught = false;
				try {
					std::lock_guard<std::mutex>lock(eps_lock);
					FC_ASSERT(midware_eps.size() > 0);
					int ep_count = midware_eps.size();
					ep_idx = con.connect_to_servers(midware_eps, counts);
					for (int i = 0; i < ep_count; i++)
					{
						int failed_count = counts[i];
						if (failed_count != 0)
							connect_counts[midware_eps[i]].second += failed_count;
					}
					FC_ASSERT(ep_idx >= 0 && ep_idx < ep_count);
					connect_counts[midware_eps[ep_idx]].first += 1;

					if (ep_idx != 0) {
						swap(midware_eps[0], midware_eps[ep_idx]);
					}
					
					return;
				}
				catch (...)
				{
					caught = true;
				}
				if (caught&&b_get_eps_from_service) {
					try
					{
						ep_idx = -1;
						std::vector<fc::ip::endpoint> temp;
						const static auto str = XWC_MIDWARE_ENDPOINT;
						temp.push_back(fc::ip::endpoint::from_string(str));
						if (temp.size() != 0)
							set_midwares(temp);
						else
							set_midwares(midware_eps_backup);
					}
					catch (...)
					{
					}
				}
			}
		}
		std::string crosschain_interface_ltc::recover_wallet(std::string &wallet_name, std::string &encrypt_passprase)
		{
			return std::string();
		}
		std::vector<fc::variant_object> crosschain_interface_ltc::transaction_history_all(std::vector<fc::mutable_variant_object> mul_param_obj) {
			std::vector<fc::variant_object> return_value;
			std::ostringstream req_body;
			req_body << "{ \"jsonrpc\": \"2.0\", \
                \"id\" : \"45\", \
				\"method\" : \"Zchain.Transaction.All.History\" ,\
				\"params\" : {\"param\":[";
			for (int i = 0; i < mul_param_obj.size(); ++i)
			{
				try
				{
					std::string chainId = mul_param_obj[i]["chainId"].as_string();
					std::string account = mul_param_obj[i]["account"].as_string();
					uint64_t blockNum = mul_param_obj[i]["blockNum"].as_uint64();
					int32_t limit = mul_param_obj[i]["limit"].as_uint64();
					req_body << "{\"chainId\":\"" << chainId << "\",";
					req_body << "\"account\":\"" << account << "\",";
					req_body << "\"blockNum\":" << blockNum << ",";
					req_body << "\"limit\":" << limit;
					if (i < mul_param_obj.size() - 1)
					{
						req_body << "},";
					}

				}
				catch (...)
				{
					if (mul_param_obj[i].find("chainId") != mul_param_obj[i].end())
					{
						std::cout << "this plugin error" << mul_param_obj[i]["chainId"].as_string() << std::endl;
					}
					else {
						std::cout << "no chainId found[" << i << "]" << std::endl;
					}
					continue;
				}
			}
			req_body << "}]}}";
			fc::http::connection_sync conn;
			connect_midware(conn);
			//conn.connect_to(fc::ip::endpoint(fc::ip::address(_config["ip"].as_string()), _config["port"].as_uint64()));
			std::cout << req_body.str() << std::endl;
			auto response = conn.request(_rpc_method, _rpc_url, req_body.str(), _rpc_headers);

			if (response.status == fc::http::reply::OK)
			{
				auto resp = fc::json::from_string(std::string(response.body.begin(), response.body.end()));
				//std::cout << std::string(response.body.begin(), response.body.end());
				auto result = resp.get_object();
				if (result.contains("result"))
				{
					auto pending_trxs = result["result"].get_array();
					for (auto one_data : pending_trxs)
					{
						return_value.push_back(one_data.get_object());
					}
				}
			}
			return return_value;
		}
	}
}
