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
#pragma once
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/committee_member_object.hpp>

namespace graphene { namespace chain {

   class wallfacer_member_create_evaluator : public evaluator<wallfacer_member_create_evaluator>
   {
      public:
         typedef wallfacer_member_create_operation operation_type;

         void_result do_evaluate( const wallfacer_member_create_operation& o );
         object_id_type do_apply( const wallfacer_member_create_operation& o );
		 void pay_fee() override;
   };

   class wallfacer_member_update_evaluator : public evaluator<wallfacer_member_update_evaluator>
   {
      public:
         typedef wallfacer_member_update_operation operation_type;

         void_result do_evaluate( const wallfacer_member_update_operation& o );
         void_result do_apply( const wallfacer_member_update_operation& o );
   };

   class committee_member_update_global_parameters_evaluator : public evaluator<committee_member_update_global_parameters_evaluator>
   {
      public:
         typedef committee_member_update_global_parameters_operation operation_type;

         void_result do_evaluate( const committee_member_update_global_parameters_operation& o );
         void_result do_apply( const committee_member_update_global_parameters_operation& o );
   };
   
   class wallfacer_member_resign_evaluator : public evaluator<wallfacer_member_resign_evaluator>
   {
   public:
       typedef wallfacer_member_resign_operation operation_type;

       void_result do_evaluate(const wallfacer_member_resign_operation& o);
       object_id_type do_apply(const wallfacer_member_resign_operation& o);
   };

   class committee_member_execute_coin_destory_operation_evaluator : public evaluator<committee_member_execute_coin_destory_operation_evaluator>
   {
   public:
	   typedef committee_member_execute_coin_destory_operation operation_type;

	   void_result do_evaluate(const committee_member_execute_coin_destory_operation& o);
	   void_result do_apply(const committee_member_execute_coin_destory_operation& o);
   };

   class miner_referendum_wallfacer_evaluator :public evaluator<miner_referendum_wallfacer_evaluator>
   {
   public:
	   typedef miner_referendum_wallfacer_operation operation_type;

	   void_result do_evaluate(const miner_referendum_wallfacer_operation& o);
	   void_result do_apply(const miner_referendum_wallfacer_operation& o);
	   bool if_evluate();
	   referendum_id_type _id;
   };


} } // graphene::chain
