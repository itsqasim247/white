/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/chain/database.hpp>
#include <graphene/chain/db_with.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/operation_history_object.hpp>

#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/referendum_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/evaluator.hpp>
#include <iostream>
#include <fc/smart_ref_impl.hpp>

namespace graphene { namespace chain {

bool database::is_known_block( const block_id_type& id )const
{
   return _fork_db.is_known_block(id) || _block_id_to_block.contains(id);
}
/**
 * Only return true *if* the transaction has not expired or been invalidated. If this
 * method is called with a VERY old transaction we will return false, they should
 * query things by blocks if they are that old.
 */
bool database::is_known_transaction( const transaction_id_type& id )const
{
	return fetch_trx(id).valid();
}

optional<trx_object> database::fetch_trx(const transaction_id_type trx_id) const
{
	try
	{
		string out;
		leveldb::ReadOptions read_options;
		auto db = get_levelDB();
		FC_ASSERT(db, "transaction api closed");
		leveldb::Status sta = db->Get(read_options, trx_id.str(), &out);
		if (sta.ok())
		{
			return fc::json::from_string(out).as<trx_object>();
		}	
	}
	catch (const fc::exception&)
	{
	}
	catch (const std::exception&)
	{
	}
	const auto& index = get_index_type<trx_index>().indices().get<by_trx_id>();
	if (index.find(trx_id) != index.end())
		return *index.find(trx_id);
	return optional<trx_object>();
}

block_id_type  database::get_block_id_for_num( uint32_t block_num )const
{ try {
   return _block_id_to_block.fetch_block_id( block_num );
} FC_CAPTURE_AND_RETHROW( (block_num) ) }

optional<signed_block> database::fetch_block_by_id( const block_id_type& id )const
{
   auto b = _fork_db.fetch_block( id );
   if( !b )
      return _block_id_to_block.fetch_optional(id);
   return b->data;
}

optional<signed_block> database::fetch_block_by_number( uint32_t num )const
{
   auto results = _fork_db.fetch_block_by_number(num);
   if( results.size() == 1 )
      return results[0]->data;
   else
      return _block_id_to_block.fetch_by_number(num);
   return optional<signed_block>();
}

const signed_transaction& database::get_recent_transaction(const transaction_id_type& trx_id) const
{
   auto& index = get_index_type<transaction_index>().indices().get<by_trx_id>();
   auto itr = index.find(trx_id);
   FC_ASSERT(itr != index.end());
   return itr->trx;
}

std::vector<block_id_type> database::get_block_ids_on_fork(block_id_type head_of_fork) const
{
  pair<fork_database::branch_type, fork_database::branch_type> branches = _fork_db.fetch_branch_from(head_block_id(), head_of_fork);
  if( !((branches.first.back()->previous_id() == branches.second.back()->previous_id())) )
  {
     edump( (head_of_fork)
            (head_block_id())
            (branches.first.size())
            (branches.second.size()) );
     assert(branches.first.back()->previous_id() == branches.second.back()->previous_id());
  }
  std::vector<block_id_type> result;
  for (const item_ptr& fork_block : branches.second)
    result.emplace_back(fork_block->id);
  result.emplace_back(branches.first.back()->previous_id());
  return result;
}

/**
 * Push block "may fail" in which case every partial change is unwound.  After
 * push block is successful the block is appended to the chain database on disk.
 *
 * @return true if we switched forks as a result of this push.
 */
bool database::push_block(const signed_block& new_block, uint32_t skip)
{
//   idump((new_block.block_num())(new_block.id())(new_block.timestamp)(new_block.previous));
   bool result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      detail::without_pending_transactions( *this, std::move(_pending_tx),
      [&]()
      {
         result = _push_block(new_block);
      });
   });
   return result;
}

bool database::_push_block(const signed_block& new_block)
{ try {
   uint32_t skip = get_node_properties().skip_flags;
   static int discard_count = 0;
   if( !(skip&skip_fork_db) )
   {
      /// TODO: if the block is greater than the head block and before the next maitenance interval
      // verify that the block signer is in the current set of active witnesses.
	   /*
	   int ncount = 0;
	   while  (_undo_db.get_active_session() > 0)
	   {
		   ncount++;
		   if (ncount > 10)
			   break;
		   fc::promise<void>::ptr p(new fc::promise<void>("_push_block::wait"));
		   try {
			   p->wait(fc::microseconds(10000));
		   }
		   catch (fc::exception& e) {
		   }
		   p.reset();
	   }
	   */
      shared_ptr<fork_item> new_head = _fork_db.push_block(new_block);
      //If the head block from the longest chain does not build off of the current head, we need to switch forks.
      if( new_head->data.previous != head_block_id() )
      {
         //If the newly pushed block is the same height as head, we get head back in new_head
         //Only switch forks if new_head is actually higher than head
         if( new_head->data.block_num() > head_block_num() )
         {
            wlog( "Switching to fork: ${id}", ("id",new_head->data.id()) );
            auto branches = _fork_db.fetch_branch_from(new_head->data.id(), head_block_id());

            // pop blocks until we hit the forked block
            while( head_block_id() != branches.second.back()->data.previous )
               pop_block();

            // push all blocks on the new fork
            for( auto ritr = branches.first.rbegin(); ritr != branches.first.rend(); ++ritr )
            {
                ilog( "pushing blocks from fork ${n} ${id}", ("n",(*ritr)->data.block_num())("id",(*ritr)->data.id()) );
                optional<fc::exception> except;
                try {
					if ((*ritr)->data.timestamp < time_point::now() - fc::seconds(7200))
					{
						discard_count++;
						if (discard_count >= 10)
							_undo_db.set_max_size(GRAPHENE_UNDO_BUFF_MAX_SIZE);
					}
					else
					{
						discard_count = 0;
						_undo_db.set_max_size(1440);
					}
                   undo_database::session session = _undo_db.start_undo_session();
                   apply_block( (*ritr)->data, skip );
                   _block_id_to_block.store( (*ritr)->id, (*ritr)->data );
                   session.commit();
                }
                catch ( const fc::exception& e ) { except = e; }
                if( except )
                {
                   wlog( "exception thrown while switching forks ${e}", ("e",except->to_detail_string() ) );
                   // remove the rest of branches.first from the fork_db, those blocks are invalid
                   while( ritr != branches.first.rend() )
                   {
                      _fork_db.remove( (*ritr)->data.id() );
                      ++ritr;
                   }
                   _fork_db.set_head( branches.second.front() );

                   // pop all blocks from the bad fork
                   while( head_block_id() != branches.second.back()->data.previous )
                      pop_block();

                   // restore all blocks from the good fork
                   for( auto ritr = branches.second.rbegin(); ritr != branches.second.rend(); ++ritr )
                   {
					   if ((*ritr)->data.timestamp < time_point::now()-fc::seconds(7200))
					   {
						   discard_count++;
						   if (discard_count >= 10)
							   _undo_db.set_max_size(GRAPHENE_UNDO_BUFF_MAX_SIZE);
					   }
					   else
					   {
						   discard_count = 0;
						   _undo_db.set_max_size(1440);
					   }
                      auto session = _undo_db.start_undo_session();
                      apply_block( (*ritr)->data, skip );
                      _block_id_to_block.store((*ritr)->id, (*ritr)->data );
                      session.commit();
                   }
                   throw *except;
                }
            }
            return true;
         }
         else return false;
      }
   }

   try {

	   if (new_block.timestamp < time_point::now() - fc::seconds(7200))
	   {
		   discard_count++;
		   if (discard_count >= 10)
			   _undo_db.set_max_size(GRAPHENE_UNDO_BUFF_MAX_SIZE);
	   }
	   else
	   {
		   discard_count = 0;
		   _undo_db.set_max_size(1440);
	   }
      auto session = _undo_db.start_undo_session();
      apply_block(new_block, skip);
      _block_id_to_block.store(new_block.id(), new_block);
      session.commit();
   } catch ( const fc::exception& e ) {
      elog("Failed to push new block:\n${e}", ("e", e.to_detail_string()));
      _fork_db.remove(new_block.id());
      throw;
   }

   return false;
} FC_CAPTURE_AND_RETHROW( (new_block) ) }

/**
 * Attempts to push the transaction into the pending queue
 *
 * When called to push a locally generated transaction, set the skip_block_size_check bit on the skip argument. This
 * will allow the transaction to be pushed even if it causes the pending block size to exceed the maximum block size.
 * Although the transaction will probably not propagate further now, as the peers are likely to have their pending
 * queues full as well, it will be kept in the queue to be propagated later when a new block flushes out the pending
 * queues.
 */
processed_transaction database::push_transaction( const signed_transaction& trx, uint32_t skip )
{ try {
   processed_transaction result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      result = _push_transaction( trx );
   } ); 
   return result;
} FC_CAPTURE_AND_RETHROW( (trx) ) }

processed_transaction database::_push_transaction( const signed_transaction& trx )
{
   // If this is the first transaction pushed after applying a block, start a new undo session.
   // This allows us to quickly rewind to the clean state of the head block, in case a new block arrives.
   if( !_pending_tx_session.valid() )
      _pending_tx_session = _undo_db.start_undo_session();

   // Create a temporary undo session as a child of _pending_tx_session.
   // The temporary session will be discarded by the destructor if
   // _apply_transaction fails.  If we make it to merge(), we
   // apply the changes.

   auto temp_session = _undo_db.start_undo_session();

   processed_transaction processed_trx;
   detail::with_skip_flags(*this, get_node_properties().skip_flags, [&]()
   {
	   processed_trx = _apply_transaction(trx);
   });

   //auto processed_trx = _apply_transaction( trx );
   _pending_tx.push_back(processed_trx);

   // notify_changed_objects();
   // The transaction applied successfully. Merge its changes into the pending block session.
   temp_session.merge();

   // notify anyone listening to pending transactions
   on_pending_transaction( trx );
   return processed_trx;
}

processed_transaction database::validate_transaction( const signed_transaction& trx,bool testing )
{
   auto session = _undo_db.start_undo_session();
   auto res= _apply_transaction( trx,testing );
   return res;
}

void database::set_gas_limit_in_block(const share_type & new_limit)
{
	_gas_limit_in_in_block = new_limit;
}
void database::clear_votes()
{
	try {
		const auto& vote_idx = get_index_type<vote_index>().indices().get<by_state>();
		auto range = vote_idx.equal_range(false);
		const auto& vote_result_idx = get_index_type<vote_result_index>().indices().get<by_vote>();
		vector<vote_object> votes;
		std::for_each(range.first, range.second, [&votes](const vote_object& b) { votes.push_back(b); });
		for (const auto& v : votes)
		{
			if (head_block_time() < v.expiration_time)
				continue;
			auto id = v.id;
			auto range_result = vote_result_idx.equal_range(boost::make_tuple(id));
			vector<vote_result_object> vote_results;
			std::for_each(range_result.first, range_result.second, [&vote_results](const vote_result_object& b) {
				vote_results.push_back(b);
			});
			const auto& v_obj = object_database::get<vote_object>(id);
			modify(v_obj, [this,&vote_results](vote_object& obj) {
				obj.finished = true;
				for (const auto& result : vote_results)
				{
					obj.result[result.index] += get_miner_obj(result.voter)->pledge_weight;
				}
			});
		}
	}FC_LOG_AND_RETHROW();
}
processed_transaction database::push_referendum(const referendum_object& referendum)
{
	try {
		transaction_evaluation_state eval_state(this);
		eval_state._is_proposed_trx = true;
		eval_state.operation_results.reserve(referendum.proposed_transaction.operations.size());
		processed_transaction ptrx(referendum.proposed_transaction);
		eval_state._trx = &ptrx;
		size_t old_applied_ops_size = _applied_ops.size();
		try{
			for (auto& op : referendum.proposed_transaction.operations)
			{
				eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
			}
			modify(referendum, [this](referendum_object& obj) {
				obj.finished = true;
				obj.expiration_time = head_block_time() + fc::seconds(XWC_REFERENDUM_VOTING_PERIOD);
			});
		}
		catch (const fc::exception& e) {
			_applied_ops.resize(old_applied_ops_size);
			elog("e", ("e", e.to_detail_string()));
			throw;
		}
		ptrx.operation_results = std::move(eval_state.operation_results);
		return ptrx;
	}FC_CAPTURE_AND_RETHROW((referendum))
	
}
processed_transaction database::push_proposal(const proposal_object& proposal)
{ try {
   transaction_evaluation_state eval_state(this);
   eval_state._is_proposed_trx = true;

   eval_state.operation_results.reserve(proposal.proposed_transaction.operations.size());
   processed_transaction ptrx(proposal.proposed_transaction);
   eval_state._trx = &ptrx;
   size_t old_applied_ops_size = _applied_ops.size();

   try {
      //auto session = _undo_db.start_undo_session(true);
	   bool del = true;
	   for (auto& op : proposal.proposed_transaction.operations)
	   {
		   eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
		   if (op.which() == operation::tag<wallfacer_member_update_operation>().value)
		   {
			   del = false;
		   }
	   }
	   if (!del)
	   {
		   modify(proposal, [this](proposal_object& obj) {
			   obj.finished = true;
			   obj.expiration_time = head_block_time() + fc::seconds(XWC_REFERENDUM_VOTING_PERIOD);
		   });
	   }
	   else
		   remove(proposal);
      
      //session.merge();
   }
   catch (const fc::exception& e) {
	   _applied_ops.resize(old_applied_ops_size);
	   elog("e", ("e", e.to_detail_string()));
	   throw;
   }
   ptrx.operation_results = std::move(eval_state.operation_results);
   return ptrx;
} FC_CAPTURE_AND_RETHROW( (proposal) ) }

signed_block database::generate_block(
   fc::time_point_sec when,
   miner_id_type witness_id,
   const fc::ecc::private_key& block_signing_private_key,
   uint32_t skip /* = 0 */
   )
{ try {
   signed_block result;
   skip |= check_gas_price;
   detail::with_skip_flags( *this, skip, [&]()
   {
      result = _generate_block( when, witness_id, block_signing_private_key );
   } );
   return result;
} FC_CAPTURE_AND_RETHROW() }

signed_block database::_generate_block(
   fc::time_point_sec when,
	miner_id_type witness_id,
   const fc::ecc::private_key& block_signing_private_key
   )
{
   try {
   uint32_t skip = get_node_properties().skip_flags;
   uint32_t slot_num = get_slot_at_time( when );
   FC_ASSERT( slot_num > 0 );
   miner_id_type scheduled_witness = get_scheduled_miner( slot_num );
   FC_ASSERT( scheduled_witness == witness_id );

   const auto& witness_obj = witness_id(*this);
   const auto& account_obj = witness_obj.miner_account(*this);
   int contract_op_in_a_block = 20;
   if(USE_CBOR_DIFF_FORK_HEIGHT <head_block_num())
	   contract_op_in_a_block = 100;
 if( !(skip & skip_miner_signature) )
      FC_ASSERT( witness_obj.signing_key == block_signing_private_key.get_public_key() );

   static const size_t max_block_header_size = fc::raw::pack_size( signed_block_header() ) + 4;
   auto maximum_block_size = get_global_properties().parameters.maximum_block_size;
   size_t total_block_size = max_block_header_size;
   signed_block pending_block;
   //
   // The following code throws away existing pending_tx_session and
   // rebuilds it by re-applying pending transactions.
   //
   // This rebuild is necessary because pending transactions' validity
   // and semantics may have changed since they were received, because
   // time-based semantics are evaluated based on the current block
   // time.  These changes can only be reflected in the database when
   // the value of the "when" variable is known, which means we need to
   // re-apply pending transactions in this method.
   //
   _pending_tx_session.reset();
   _pending_tx_session = _undo_db.start_undo_session();
   uint64_t postponed_tx_count = 0;
   uint64_t postponed_tx_count_by_gas_limit = 0;
   uint64_t postponed_tx_count_by_contract_op_limit = 0;
   // pop pending state (reset to head block state)
   reset_current_collected_fee();
   map<string, int > temp_signature;
   _current_gas_in_block = 0;
   for( const processed_transaction& tx : _pending_tx )
   {
      size_t new_total_size = total_block_size + fc::raw::pack_size( tx );
	  bool continue_if = false;
      // postpone transaction if it would make block too big
      if( new_total_size >= maximum_block_size )
      {
         postponed_tx_count++;
         continue;
      }
	  bool related_with_contract = false;
	  int contract_op_in_trx = 0;
	  gas_count_type gas_count = 0; 
	  for (auto& op : tx.operations)
	  {

		  switch (op.which())
		  {
		  case operation::tag<chain::contract_register_operation>::value:
		  {
			  printf("Got A contract_register_operation\n");
			  gas_count+=op.get<contract_register_operation>().init_cost;
			  related_with_contract = true;
			  contract_op_in_trx++;
			  break;
		  }
		  case operation::tag<chain::contract_upgrade_operation>::value:
		  {
			  gas_count += op.get<contract_upgrade_operation>().invoke_cost;
			  related_with_contract = true;
			  contract_op_in_trx++;
			  break;
		  }
		  case operation::tag<chain::contract_invoke_operation>::value:
		  {
			  gas_count += op.get<contract_invoke_operation>().invoke_cost;
			  related_with_contract = true;
			  contract_op_in_trx++;
			  break;
		  }
		  case operation::tag<chain::transfer_contract_operation>::value:
		  {
			  gas_count += op.get<transfer_contract_operation>().invoke_cost;
			  related_with_contract = true;
			  contract_op_in_trx++;
			  break;
		  }
		  case operation::tag<chain::native_contract_register_operation>::value:
		  {
			  gas_count += op.get<native_contract_register_operation>().init_cost;
			  related_with_contract = true;
			  contract_op_in_trx++;
			  break;
		  }
		  }
	  }
	  if (related_with_contract&&(_current_gas_in_block+gas_count > _gas_limit_in_in_block))
	  {
		      printf("Gas limit block reached\n");
			  postponed_tx_count_by_gas_limit++;
			  continue;
	  }
	  if (related_with_contract&&contract_op_in_a_block<contract_op_in_trx)
	  {
		  printf("contract op limit reached in block\n");
		  postponed_tx_count_by_contract_op_limit++;
		  continue;
	  }
	  contract_op_in_a_block -= contract_op_in_trx;
      try
      {
         auto temp_session = _undo_db.start_undo_session();
         
         processed_transaction ptx = _apply_transaction( tx );
         temp_session.merge();

         // We have to recompute pack_size(ptx) because it may be different
         // than pack_size(tx) (i.e. if one or more results increased
         // their size)
         total_block_size += fc::raw::pack_size( ptx );
         pending_block.transactions.push_back( ptx );
      }
      catch ( const fc::exception& e )
      {
         // Do nothing, transaction will not be re-applied
         wlog( "Transaction was not processed while generating block due to ${e}", ("e", e) );
         wlog( "The transaction was ${t}", ("t", tx) );
      }
   }
   if( postponed_tx_count > 0 )
   {
      wlog( "Postponed ${n} transactions due to block size limit", ("n", postponed_tx_count) );
   }
   if (postponed_tx_count_by_gas_limit > 0)
   {
	   wlog("Postponed ${n} transactions due to block gas limit reached", ("n", postponed_tx_count_by_gas_limit));
   }
   if (postponed_tx_count_by_contract_op_limit > 0)
   {
	   wlog("Postponed ${n} transactions due to block contract op limit reached", ("n", postponed_tx_count_by_contract_op_limit));
   }
   _pending_tx_session.reset();


   // We have temporarily broken the invariant that
   // _pending_tx_session is the result of applying _pending_tx, as
   // _pending_tx now consists of the set of postponed transactions.
   // However, the push_block() call below will re-create the
   // _pending_tx_session.

   pending_block.previous = head_block_id();
   pending_block.timestamp = when;
   pending_block.trxfee = _total_collected_fees[asset_id_type(0)];
   pending_block.transaction_merkle_root = pending_block.calculate_merkle_root();
   pending_block.miner = witness_id;



   //Add a random number generation process to the signature
   const uint32_t last_produced_block_num = witness_obj.last_confirmed_block_num;

   const optional<SecretHashType>& prev_secret_hash = witness_obj.next_secret_hash;
   if (prev_secret_hash.valid())
   {
	   const uint32_t last_signing_key_change_block_num = witness_obj.last_change_signing_key_block_num;

	   if (last_produced_block_num > last_signing_key_change_block_num)
	   {
		   pending_block.previous_secret = get_secret(last_produced_block_num, block_signing_private_key);
		   FC_ASSERT(fc::ripemd160::hash(pending_block.previous_secret) == *prev_secret_hash);
	   }
	   else
	   {
		   // We need to use the old key to reveal the previous secret
		   
		   pending_block.previous_secret = *prev_secret_hash;
	   }

	   
   }

   pending_block.next_secret_hash = fc::ripemd160::hash(get_secret(pending_block.block_num(), block_signing_private_key));




   if( !(skip & skip_miner_signature) )
      pending_block.sign( block_signing_private_key );

   // TODO:  Move this to _push_block() so session is restored.
   if( !(skip & skip_block_size_check) )
   {
      FC_ASSERT( fc::raw::pack_size(pending_block) <= get_global_properties().parameters.maximum_block_size );
   }

   push_block( pending_block, skip );

   return pending_block;
} FC_CAPTURE_AND_RETHROW( (witness_id) ) }


SecretHashType database::get_secret(uint32_t block_num,
	const fc::ecc::private_key& block_signing_private_key)
{
	head_block_id();
	block_id_type header_id;
	if (block_num != uint32_t(-1) && block_num > 1)
	{
		header_id = get_block_id_for_num(block_num-1);
	}

	fc::sha512::encoder key_enc;
	fc::raw::pack(key_enc, block_signing_private_key);
	fc::sha512::encoder enc;
	fc::raw::pack(enc, key_enc.result());
	fc::raw::pack(enc, header_id);

	return fc::ripemd160::hash(enc.result());
}
/**
 * Removes the most recent block from the database and
 * undoes any changes it made.
 */
void database::pop_block()
{ try {
   _pending_tx_session.reset();
   auto head_id = head_block_id();
   optional<signed_block> head_block = fetch_block_by_id( head_id );
   GRAPHENE_ASSERT( head_block.valid(), pop_empty_chain, "there are no blocks to pop" );

   _fork_db.pop_block();
   _block_id_to_block.remove( head_id );
   pop_undo();
   vector<signed_transaction> txs(head_block->transactions.begin(),head_block->transactions.end());
   removed_trxs(txs);
   _popped_tx.insert( _popped_tx.begin(), head_block->transactions.begin(), head_block->transactions.end() );
} FC_CAPTURE_AND_RETHROW() }

void database::clear_pending()
{ try {
   assert( (_pending_tx.size() == 0) || _pending_tx_session.valid() );
   _pending_tx.clear();
   _pending_tx_session.reset();
} FC_CAPTURE_AND_RETHROW() }

uint32_t database::push_applied_operation( const operation& op )
{
   _applied_ops.emplace_back(op);
   operation_history_object& oh = *(_applied_ops.back());
   oh.block_num    = _current_block_num;
   oh.trx_in_block = _current_trx_in_block;
   oh.op_in_trx    = _current_op_in_trx;
   oh.virtual_op   = _current_virtual_op++;
   return _applied_ops.size() - 1;
}
void database::set_applied_operation_result( uint32_t op_id, const operation_result& result )
{
   assert( op_id < _applied_ops.size() );
   if( _applied_ops[op_id] )
      _applied_ops[op_id]->result = result;
   else
   {
      elog( "Could not set operation result (head_block_num=${b})", ("b", head_block_num()) );
   }
}

const vector<optional< operation_history_object > >& database::get_applied_operations() const
{
   return _applied_ops;
}

//////////////////// private methods ////////////////////

void database::apply_block( const signed_block& next_block, uint32_t skip )
{
   auto block_num = next_block.block_num();
   if( _checkpoints.size() && _checkpoints.rbegin()->second != block_id_type() )
   {
      auto itr = _checkpoints.find( block_num );
      if( itr != _checkpoints.end() )
         FC_ASSERT( next_block.id() == itr->second, "Block did not match checkpoint", ("checkpoint",*itr)("block_id",next_block.id()) );

      if( _checkpoints.rbegin()->first >= block_num )
         skip = ~0;// WE CAN SKIP ALMOST EVERYTHING
   }

   detail::with_skip_flags( *this, skip, [&]()
   {
      _apply_block( next_block );
   } );
   return;
}

void database::_apply_block( const signed_block& next_block )
{ try {
   uint32_t next_block_num = next_block.block_num();
   uint32_t skip = get_node_properties().skip_flags;
   _applied_ops.clear();
   reset_current_collected_fee();
   FC_ASSERT( (skip & skip_merkle_check) || next_block.transaction_merkle_root == next_block.calculate_merkle_root(), "", ("next_block.transaction_merkle_root",next_block.transaction_merkle_root)("calc",next_block.calculate_merkle_root())("next_block",next_block)("id",next_block.id()) );

   const miner_object& signing_witness = validate_block_header(skip, next_block);
   const auto& global_props = get_global_properties();
   const auto& dynamic_global_props = get<dynamic_global_property_object>(dynamic_global_property_id_type());
   bool maint_needed = (dynamic_global_props.next_maintenance_time <= next_block.timestamp);

   _current_block_num    = next_block_num;
   _current_trx_in_block = 0;
   _current_secret_key = next_block.previous_secret;
   _current_contract_call_num = 0;
  
   map<string, int> temp_signature;
   for( const auto& trx : next_block.transactions )
   {
      /* We do not need to push the undo state for each transaction
       * because they either all apply and are valid or the
       * entire block fails to apply.  We only need an "undo" state
       * for transactions when validating broadcast transactions or
       * when building a block.
       */
	  const auto& apply_trx_res = apply_transaction(trx, skip);
	  FC_ASSERT(apply_trx_res.operation_results == trx.operation_results, "operation apply result not same with result in block");
      ++_current_trx_in_block;
	  _push_transaction_tx_ids.emplace(trx.id());
	  //store_transactions(signed_transaction(trx));
   }
 //  if(next_block_num == 1901662) {
	//printf("next_block.trxfee=%lld, _total_collected_fees[asset_id_type(0)]=%lld\n", next_block.trxfee.value, _total_collected_fees[asset_id_type(0)].value);
 //  }
   if(next_block.trxfee != _total_collected_fees[asset_id_type(0)]) {
      printf("next_block trxfee: %d, _total_collected_fee: %d\n", next_block.trxfee.value, _total_collected_fees[asset_id_type(0)].value);
   }
   if( !(_current_block_num < 8150000 && _current_block_num > 8000000))
   {
	   FC_ASSERT(next_block.trxfee == _total_collected_fees[asset_id_type(0)], "trxfee should be the same with ");
   }
   if (signing_witness.last_confirmed_block_num > signing_witness.last_change_signing_key_block_num) {
	   FC_ASSERT(fc::ripemd160::hash(next_block.previous_secret) == fetch_block_by_id(get_block_id_for_num(signing_witness.last_confirmed_block_num))->next_secret_hash);
   }
   
   if (_current_block_num == XWC_CROSSCHAIN_ERC_FORK_HEIGHT)
   {
	   auto& wallfacer_member_db = get_index_type<wallfacer_member_index>().indices().get<by_id>();
	   auto change_func = [&](object_id_type id) {
       auto change_iter = wallfacer_member_db.find(id);
       if (wallfacer_member_db.end() != change_iter)
       {         
         modify(*change_iter, [&](wallfacer_member_object& obj) {
           obj.formal = true;
           obj.wallfacer_type = PERMANENT;
         });
       }
	   };
	   change_func(object_id_type(1, 5, 27));
	   change_func(object_id_type(1, 5, 28));
	   change_func(object_id_type(1, 5, 29));
	   change_func(object_id_type(1, 5, 30));
     change_func(object_id_type(1, 5, 31));
          
	   auto wallfacer_range = get_index_type<wallfacer_member_index>().indices().get<by_wallfacer_type>().equal_range(PERMANENT);
	   for (auto iter : boost::make_iterator_range(wallfacer_range.first,wallfacer_range.second))
	   {
       auto& wallfacer_index_db = get_index_type<wallfacer_member_index>().indices().get<by_id>();
       auto change_iter = wallfacer_index_db.find(iter.id);
       if (wallfacer_index_db.end() == change_iter)
       {
         continue;
       }
       		  	  
		  if (iter.id == object_id_type(1, 5, 27) ||
          iter.id == object_id_type(1, 5, 28) ||
          iter.id == object_id_type(1, 5, 29) ||
          iter.id == object_id_type(1, 5, 30) ||
          iter.id == object_id_type(1, 5, 31))
		  {			  
			  modify(*change_iter, [&](wallfacer_member_object& obj) {
				  obj.formal = true;
			  });
		  }
		  else {
			  modify(*change_iter, [&](wallfacer_member_object& obj) {
				  obj.formal = false;
			  });
		  }
	   }

       // eth
	   auto& multisig_db = get_index_type<multisig_address_index>().indices().get<by_id>();
	   for (int i = 0; i < 15; i++) {
		   auto multisig_iter = multisig_db.find(object_id_type(2, 8, 240 + i));
		   if (multisig_iter != multisig_db.end())
		   {
			   if (multisig_iter->multisig_account_pair_object_id == object_id_type(2, 7, 0) )
			   {
				   modify(*multisig_iter, [&](multisig_address_object& obj) {
					   obj.multisig_account_pair_object_id = object_id_type(2, 7, 26);
				   });
			   }
		   }
	   }
	  
       // usdt
	   for (int i = 0; i < 15; i++) {
		   auto multisig_iter = multisig_db.find(object_id_type(2, 8, 255 + i));
		   if (multisig_iter != multisig_db.end())
		   {
			   if (multisig_iter->multisig_account_pair_object_id == object_id_type(2, 7, 0) )
				   {
				   modify(*multisig_iter, [&](multisig_address_object& obj) {
					   obj.multisig_account_pair_object_id = object_id_type(2, 7, 27);
				   });
			   }
		   }
	   }

     // block
     const std::vector<std::string> block_addreess = {
        "XWCNUC2czFdhu7XfoWq3p92fb4RKmgdsVcuEJ",
        "XWCNb253NYpyBWjw9ojmKUXnerowwZz4Diqbg",
        "XWCNYeYdxFDSyurTTi5mdfC3hcfKVdsqZW8dN",
        "XWCNU5tajk9aGDzJGEHmRHrjGPyfwMTZnxzPe" };
     address account_foudation("XWCNNJ38q9Jo3uLPihM4YEYKXFDqdMYTDBzhK");
     for (const auto block_addrees : block_addreess)
     {
       address account_blocked(block_addrees);
       auto asset_xwc = get_asset("XWC");
       if (asset_xwc.valid())
       {
         auto block_asset_xwc = get_balance(account_blocked, asset_xwc->id);
         if (block_asset_xwc > asset(0, asset_xwc->id))
         {
           adjust_balance(account_foudation, block_asset_xwc);
           adjust_balance(account_blocked, -block_asset_xwc);
         }
       }

       auto asset_btc = get_asset("BTC");
       if (asset_btc.valid())
       {
         auto block_asset_btc = get_balance(account_blocked, asset_btc->id);
         if (block_asset_btc > asset(0, asset_btc->id))
         {
           adjust_balance(account_foudation, block_asset_btc);
           adjust_balance(account_blocked, -block_asset_btc);
         }
       }

       auto asset_ltc = get_asset("LTC");
       if (asset_ltc.valid())
       {
         auto block_asset_ltc = get_balance(account_blocked, asset_ltc->id);
         if (block_asset_ltc > asset(0, asset_ltc->id))
         {
           adjust_balance(account_foudation, block_asset_ltc);
           adjust_balance(account_blocked, -block_asset_ltc);
         }
       }

       auto asset_eth = get_asset("ETH");
       if (asset_eth.valid())
       {
         auto block_asset_eth = get_balance(account_blocked, asset_eth->id);
         if (block_asset_eth > asset(0, asset_eth->id))
         {
           adjust_balance(account_foudation, block_asset_eth);
           adjust_balance(account_blocked, -block_asset_eth);
         }
       }

       auto asset_ercusdt = get_asset("ERCUSDT");
       if (asset_ercusdt.valid())
       {
         auto block_asset_ercusdt = get_balance(account_blocked, asset_ercusdt->id);
         if (block_asset_ercusdt > asset(0, asset_ercusdt->id))
         {
           adjust_balance(account_foudation, block_asset_ercusdt);
           adjust_balance(account_blocked, -block_asset_ercusdt);
         }
       }

       auto asset_doge = get_asset("DOGE");
       if (asset_doge.valid())
       {
         auto block_asset_doge = get_balance(account_blocked, asset_doge->id);
         if (block_asset_doge > asset(0, asset_doge->id))
         {
           adjust_balance(account_foudation, block_asset_doge);
           adjust_balance(account_blocked, -block_asset_doge);
         }
       }
     }
   }

   //_total_collected_fees[asset_id_type(0)] = share_type(0);
   update_global_dynamic_data(next_block);
   update_signing_miner(signing_witness, next_block);
   update_last_irreversible_block();

   // Are we at the maintenance interval?
   if( maint_needed )
      perform_chain_maintenance(next_block, global_props);

   create_block_summary(next_block);
   clear_expired_transactions();
   clear_expired_proposals();
   clear_expired_orders();
   determine_referendum_detailes();
   update_expired_feeds();
   update_withdraw_permissions();

   // n.b., update_maintenance_flag() happens this late
   // because get_slot_time() / get_slot_at_time() is needed above
   // TODO:  figure out if we could collapse this function into
   // update_global_dynamic_data() as perhaps these methods only need
   // to be called for header validation?
   update_maintenance_flag( maint_needed );
   update_fee_pool();
   pay_miner(next_block.miner,asset(next_block.trxfee));
   process_bonus();
   update_miner_schedule();
   update_witness_random_seed(next_block.previous_secret);
   if( !_node_property_object.debug_updates.empty() )
      apply_debug_updates();
   // notify observers that the block has been applied
   applied_block( next_block ); //emit
   clear_votes();
   _applied_ops.clear();

   notify_changed_objects();
} FC_CAPTURE_AND_RETHROW( (next_block.block_num()) )  }



processed_transaction database::apply_transaction(const signed_transaction& trx, uint32_t skip)
{
   processed_transaction result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      result = _apply_transaction(trx);
   });
   return result;
}

processed_transaction database::_apply_transaction(const signed_transaction& trx,bool testing)
{ try {
   uint32_t skip = get_node_properties().skip_flags;

   if( true || !(skip&skip_validate) )   /* issue #505 explains why this skip_flag is disabled */
      trx.validate();

   auto& trx_idx = get_index_type<trx_index>();
   const chain_id_type& chain_id = get_chain_id();
   auto trx_id = trx.id();    
   FC_ASSERT( (skip & skip_transaction_dupe_check) ||
              !fetch_trx(trx_id).valid() );
   transaction_evaluation_state eval_state(this);
   const chain_parameters& chain_parameters = get_global_properties().parameters;
   eval_state._trx = &trx;
   eval_state.testing = testing;
   if (!testing) {
	   if (!(skip & (skip_transaction_signatures | skip_authority_check)))
	   {
		   auto get_addresses = [&](address addr) {
			   const auto& bal_idx = get_index_type<balance_index>();
			   const auto& by_owner_idx = bal_idx.indices().get<by_owner>();
			   auto iter = by_owner_idx.find(boost::make_tuple(addr, asset_id_type(0)));
			   if (iter != by_owner_idx.end())
			   {
				   if (iter->multisignatures.valid())
				   {
					   auto required = iter->multisignatures->begin()->first;
					   auto addresses = iter->multisignatures->begin()->second;
					   return std::tuple < address, int, fc::flat_set<public_key_type>>(addr, required, addresses);
				   }
			   }
			   return std::tuple < address, int, fc::flat_set<public_key_type>>(addr, 0, fc::flat_set<public_key_type>());
		   };
		   auto is_blocked_address = [&](address addr) {
			   // need to check
			   const auto& blocked_idx = get_index_type<blocked_index>().indices().get<by_address>();
			   if (blocked_idx.find(addr) != blocked_idx.end())
				   return true;
			   return false;
		   };
		   auto is_whited_ops = [&](address addr, int op) {
			   const auto& white_idx = get_index_type<whiteOperation_index>().indices().get<by_address>();
			   if (white_idx.find(addr) == white_idx.end())
			   {
				   return false;
			   }
			   auto iter = white_idx.find(addr);
			   if (iter->ops.count(op))
				   return true;
			   return false;
		   };
		   trx.verify_authority(chain_id, get_addresses, is_blocked_address,is_whited_ops, get_global_properties().parameters.max_authority_depth);
	   }
   }
   //Skip all manner of expiration and TaPoS checking if we're on block 1; It's impossible that the transaction is
   //expired, and TaPoS makes no sense as no blocks exist.
   if( BOOST_LIKELY(head_block_num() > 0) )
   {
      if( !(skip & skip_tapos_check) )
      {
         const auto& tapos_block_summary = block_summary_id_type( trx.ref_block_num )(*this);
         //Verify TaPoS block summary has correct ID prefix, and that this block's time is not past the expiration
         FC_ASSERT( trx.ref_block_prefix == tapos_block_summary.block_id._hash[1] );
		 
      }

      fc::time_point_sec now = head_block_time();
      FC_ASSERT( trx.expiration <= now + chain_parameters.maximum_time_until_expiration, "",
                 ("trx.expiration",trx.expiration)("now",now)("max_til_exp",chain_parameters.maximum_time_until_expiration));
      FC_ASSERT( now <= trx.expiration, "", ("now",now)("trx.exp",trx.expiration) );
   }

   //Insert transaction into unique transactions database.
   if( !(skip & skip_transaction_dupe_check) )
   {
	   
      create<transaction_object>([&](transaction_object& transaction) {
         transaction.trx_id = trx.id();
         transaction.trx = trx;
      });
   }
   eval_state.operation_results.reserve(trx.operations.size());

   //Finally process the operations
   processed_transaction ptrx(trx);
   _current_op_in_trx = 0;
   bool skip_exec = skip & skip_contract_exec;
   for( const auto& op : ptrx.operations ) 
   {
	   if (skip_exec&&is_contract_operation(op))
	   {
		   eval_state.operation_results.emplace_back(void_result());
	   }
	   else
	   {
		   eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
	   }
      ++_current_op_in_trx;
   }
   ptrx.operation_results = std::move(eval_state.operation_results);

   //Make sure the temp account has no non-zero balances
   const auto& index = get_index_type<account_balance_index>().indices().get<by_account_asset>();
   auto range = index.equal_range( boost::make_tuple( GRAPHENE_TEMP_ACCOUNT ) );
   std::for_each(range.first, range.second, [](const account_balance_object& b) { FC_ASSERT(b.balance == 0); });

   return ptrx;
} FC_CAPTURE_AND_RETHROW( (trx) ) }

operation_result database::apply_operation(transaction_evaluation_state& eval_state, const operation& op)
{ try {
   int i_which = op.which();
   uint64_t u_which = uint64_t( i_which );
   if( i_which < 0 )
      assert( "Negative operation tag" && false );
   if( u_which >= _operation_evaluators.size() )
      assert( "No registered evaluator for this operation" && false );
   unique_ptr<op_evaluator>& eval = _operation_evaluators[ u_which ];
   if( !eval )
      assert( "No registered evaluator for this operation" && false );
  // auto op_id = push_applied_operation( op );
   auto result = eval->evaluate( eval_state, op, true );
  // set_applied_operation_result( op_id, result );
   eval_state.op_num++;
   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

unique_ptr<op_evaluator>& database::get_evaluator(const operation& op)
{
	int i_which = op.which();
	uint64_t u_which = uint64_t(i_which);
	if (i_which < 0)
		assert("Negative operation tag" && false);
	if (u_which >= _operation_evaluators.size())
		assert("No registered evaluator for this operation" && false);
	unique_ptr<op_evaluator>& eval = _operation_evaluators[u_which];
	if (!eval)
		assert("No registered evaluator for this operation" && false);
	return eval;
}
const miner_object& database::validate_block_header( uint32_t skip, const signed_block& next_block )const
{
   FC_ASSERT( head_block_id() == next_block.previous, "", ("head_block_id",head_block_id())("next.prev",next_block.previous) );
   FC_ASSERT( head_block_time() < next_block.timestamp, "", ("head_block_time",head_block_time())("next",next_block.timestamp)("blocknum",next_block.block_num()) );
   const miner_object& witness = next_block.miner(*this);
   if( !(skip&skip_miner_signature) ) 
      FC_ASSERT( next_block.validate_signee( witness.signing_key ) );

   if( !(skip&skip_witness_schedule_check) )
   {
      uint32_t slot_num = get_slot_at_time( next_block.timestamp );
      FC_ASSERT( slot_num > 0 );

      miner_id_type scheduled_miner = get_scheduled_miner( slot_num );

      FC_ASSERT( next_block.miner == scheduled_miner, "Witness produced block at wrong time",
                 ("block witness",next_block.miner)("scheduled",scheduled_miner)("slot_num",slot_num) );
   }

   return witness;
}

void database::create_block_summary(const signed_block& next_block)
{
   block_summary_id_type sid(next_block.block_num() & 0xffff );
   modify( sid(*this), [&](block_summary_object& p) {
         p.block_id = next_block.id();
   });
}

void database::add_checkpoints( const flat_map<uint32_t,block_id_type>& checkpts )
{
   for( const auto& i : checkpts )
      _checkpoints[i.first] = i.second;
}

bool database::before_last_checkpoint()const
{
   return (_checkpoints.size() > 0) && (_checkpoints.rbegin()->first >= head_block_num());
}

} }
