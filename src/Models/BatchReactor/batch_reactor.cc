// batch_reactor.cc
// Implements the BatchReactor class
#include "batch_reactor.h"

#include <sstream>
#include <cmath>

#include <boost/lexical_cast.hpp>

#include "cyc_limits.h"
#include "context.h"
#include "error.h"
#include "logger.h"
#include "generic_resource.h"
#include "market_model.h"

namespace cycamore {

// static members
std::map<Phase, std::string> BatchReactor::phase_names_ =
    std::map<Phase, std::string>();

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BatchReactor::BatchReactor(cyclus::Context* ctx)
    : cyclus::FacilityModel(ctx),
      cyclus::Model(ctx),
      process_time_(1),
      preorder_time_(0),
      start_time_(-1),
      n_batches_(1),
      n_load_(1),
      n_reserves_(1),
      refuel_time_(0),
      batch_size_(1),
      in_commodity_(""),
      in_recipe_(""),
      out_commodity_(""),
      out_recipe_(""),
      phase_(INITIAL) {
  preCore_.capacity(cyclus::kBuffInfinity);
  inCore_.capacity(cyclus::kBuffInfinity);
  postCore_.capacity(cyclus::kBuffInfinity);
  if (phase_names_.empty()) {
    SetUpPhaseNames_();
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BatchReactor::~BatchReactor() {}
  
//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BatchReactor::schema() {
  return
    "  <!-- cyclus::Material In/Out  -->         \n"
    "  <element name=\"fuel_input\">             \n"
    "   <ref name=\"incommodity\"/>              \n"
    "   <ref name=\"inrecipe\"/>                 \n"
    "  </element>                                \n"
    "  <element name=\"fuel_output\">            \n"
    "   <ref name=\"outcommodity\"/>             \n"
    "   <ref name=\"outrecipe\"/>                \n"
    "  </element>                                \n"
    "                                            \n"
    "  <!-- Facility Parameters -->              \n"
    "  <element name=\"processtime\">            \n"
    "    <data type=\"nonNegativeInteger\"/>     \n"
    "  </element>                                \n"
    "  <element name=\"nbatches\">               \n"
    "    <data type=\"nonNegativeInteger\"/>     \n"
    "  </element>                                \n"
    "  <element name =\"batchsize\">             \n"
    "    <data type=\"double\"/>                 \n"
    "  </element>                                \n"
    "  <optional>                                \n"
    "    <element name =\"refueltime\">          \n"
    "      <data type=\"nonNegativeInteger\"/>   \n"
    "    </element>                              \n"
    "  </optional>                               \n"
    "  <optional>                                \n"
    "    <element name =\"orderlookahead\">      \n"
    "      <data type=\"nonNegativeInteger\"/>   \n"
    "    </element>                              \n"
    "  </optional>                               \n"
    "  <optional>                                \n"
    "    <element name =\"norder\">              \n"
    "      <data type=\"nonNegativeInteger\"/>   \n"
    "    </element>                              \n"
    "  </optional>                               \n"
    "  <optional>                                \n"
    "    <element name =\"nreload\">             \n"
    "      <data type=\"nonNegativeInteger\"/>   \n"
    "    </element>                              \n"
    "  </optional>                               \n"
    "                                            \n"
    "  <!-- Power Production  -->                \n"
    "  <element name=\"commodity_production\">   \n"
    "   <element name=\"commodity\">             \n"
    "     <data type=\"string\"/>                \n"
    "   </element>                               \n"
    "   <element name=\"capacity\">              \n"
    "     <data type=\"double\"/>                \n"
    "   </element>                               \n"
    "   <element name=\"cost\">                  \n"
    "     <data type=\"double\"/>                \n"
    "   </element>                               \n"
    "  </element>                                \n";
};

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::InitModuleMembers(cyclus::QueryEngine* qe) {
  using std::string;
  using boost::lexical_cast;

  // in/out
  cyclus::QueryEngine* input = qe->QueryElement("fuel_input");
  in_commodity(input->GetElementContent("incommodity"));
  in_recipe(input->GetElementContent("inrecipe"));
  
  cyclus::QueryEngine* output = qe->QueryElement("fuel_output");
  out_commodity(output->GetElementContent("outcommodity"));
  out_recipe(output->GetElementContent("outrecipe"));

  // facility data required
  string data;
  data = qe->GetElementContent("processtime");
  process_time(lexical_cast<int>(data));
  data = qe->GetElementContent("nbatches");
  n_batches(lexical_cast<int>(data));
  data = qe->GetElementContent("batchsize");
  batch_size(lexical_cast<int>(data));

  // facility data optional  
  int time =
      cyclus::GetOptionalQuery<int>(qe, "refueltime", refuel_time());
  refuel_time(time);
  time =
      cyclus::GetOptionalQuery<int>(qe, "orderlookahead", preorder_time());
  preorder_time(time);

  int n = 
      cyclus::GetOptionalQuery<int>(qe, "nreload", n_load());
  n_load(n);
  n = cyclus::GetOptionalQuery<int>(qe, "norder", n_load());
  n_reserves(n);

  // commodity production
  cyclus::QueryEngine* commodity = qe->QueryElement("commodity_production");
  cyclus::Commodity commod(commodity->GetElementContent("commodity"));
  AddCommodity(commod);
  data = commodity->GetElementContent("capacity");
  cyclus::CommodityProducer::SetCapacity(commod,
                                         lexical_cast<double>(data));
  data = commodity->GetElementContent("cost");
  cyclus::CommodityProducer::SetCost(commod,
                                     lexical_cast<double>(data));
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::Model* BatchReactor::Clone() {
  BatchReactor* m = new BatchReactor(context());
  m->InitFrom(this);

  // in/out
  m->in_commodity(in_commodity());
  m->out_commodity(out_commodity());
  m->in_recipe(in_recipe());
  m->out_recipe(out_recipe());

  // facility params
  m->process_time(process_time());
  m->preorder_time(preorder_time());
  m->start_time(start_time());
  m->n_batches(n_batches());
  m->n_load(n_load());
  m->n_reserves(n_reserves());
  m->batch_size(batch_size());

  // commodity production
  m->CopyProducedCommoditiesFrom(this);

  return m;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BatchReactor::str() {
  std::stringstream ss;
  ss << cyclus::FacilityModel::str();
  ss << " has facility parameters {"
     << ", Process Time = " << process_time()
     << ", Refuel Time = " << refuel_time()
     << ", Core Loading = " << n_batches() * batch_size()
     << ", Batches Per Core = " << n_batches()
     << ", converts commodity '" << in_commodity()
     << "' into commodity '" << out_commodity()
     << "'}";
  return ss.str();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Deploy(cyclus::Model* parent) {
  FacilityModel::Deploy(parent);
  phase(INITIAL);
  LOG(cyclus::LEV_DEBUG2, "BReact") << "Batch Reactor entering the simuluation";
  LOG(cyclus::LEV_DEBUG2, "BReact") << str();
}


//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::HandleTick(int time) {
  switch (phase()) {
    case PROCESS:
      if (time == end_time()) {
        for (int i = 0; i < n_load(); i++) {
          MoveBatchOut_();
        }
        phase(WAITING);
      }
      break;

    case WAITING:
      if (n_core() == n_batches() &&
          end_time() + refuel_time() <= context()->time()) {
        phase(PROCESS);
      } 
      break;
      
    case INITIAL:
      // special case for a core primed to go
      if (n_core() == n_batches()) phase(PROCESS);
      break;
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::HandleTock(int time) {
  switch (phase()) {
    case INITIAL: // falling through
    case WAITING:
      Refuel_();
      break;
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
virtual std::set<cyclus::RequestPortfolio<cyclus::Material>::Ptr>
BatchReactor::GetMatlRequests() {
  std::set<cyclus::RequestPortfolio<cyclus::Material>::Ptr> set;
  double order_size;

  // switch (phase()) {
  //   case INITIAL:
  //     // reserves_.quantity() < batch_size() from tick filling
  //     int n_orders = n_batches() - n_core(); 
  //     if (preorder_time() == 0) n_orders += n_reserves();
  //     order_size = n_orders * batch_size() - reserves_.quantity();
  //     if (order_size > 0) {
  //       RequestPortfolio<cyclus::Material>::Ptr p = GetOrder_(order_size);
  //       set.insert(p);
  //     }
  //     break;

  //   case default:
  order_size = n_reserves() * batch_size() - reserves_.quantity();
  if (order_time() <= context()->time() &&
      order_size > cyclus::eps()) {
    RequestPortfolio<cyclus::Material>::Ptr p = GetOrder_(order_size);
    set.insert(p);
  }
  //     break;
  // }

  return set;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
virtual void BatchReactor::AcceptMatlTrades(
    const std::vector< std::pair<cyclus::Trade<cyclus::Material>,
                                 cyclus::Material::Ptr> >& responses) {
  cyclus::Material::Ptr mat = responses.at(0).second;
  for (int i = 1; i < responses.size(); i++) {
    mat->Absorb(responses.at(i).second);
  }
  AddBatches_(mat);
}
  
//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
virtual std::set<cyclus::BidPortfolio<cyclus::Material>::Ptr>
BatchReactor::GetMatlBids(const cyclus::CommodMap<cyclus::Material>::type&
                          commod_requests) {
  using cyclus::Bid;
  using cyclus::BidPortfolio;
  using cyclus::CapacityConstraint;
  using cyclus::Converter;
  using cyclus::Material;
  using cyclus::Request;
  
  const std::vector<Request<Material>::Ptr>& requests =
      commod_requests.at(out_commodity_);

  std::set<BidPortfolio<Material>::Ptr> ports;
  BidPortfolio<Material>::Ptr port(new BidPortfolio<Material>());

  std::vector<Request<Material>::Ptr>::const_iterator it;
  for (it = requests.begin(); it != requests.end(); ++it) {
    const Request<Material>::Ptr req = *it;
    double qty = req->target->quantity();
    if (qty < storage_.quantity()) {
      Material::Ptr offer =
          Material::CreateUntracked(qty, context()->GetRecipe(out_recipe_));
      Bid<Material>::Ptr bid(new Bid<Material>(req, offer, this));
      port->AddBid(bid);
    }
  }

  CapacityConstraint<Material> cc(storage_.quantity());
  port->AddConstraint(cc);
  ports.insert(port);
  return ports;
}
  
//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
virtual void BatchReactor::GetMatlTrades(
    const std::vector< cyclus::Trade<cyclus::Material> >& trades,
    std::vector<std::pair<cyclus::Trade<cyclus::Material>,
                          cyclus::Material::Ptr> >& responses) {
  using cyclus::Material;
  using cyclus::Trade;

  std::vector< cyclus::Trade<cyclus::Material> >::const_iterator it;
  for (it = trades.begin(); it != trades.end(); ++it) {
    double qty = it->amt;
    
    // pop amount from inventory and blob it into one material
    std::vector<Material::Ptr> manifest =
        ResCast<Material>(storage_.PopQty(qty));  
    Material::Ptr response = manifest[0];
    for (int i = 1; i < manifest.size(); i++) {
      response->Absorb(manifest[i]);
    }

    responses.push_back(std::make_pair(*it, response));
    LOG(cyclus::LEV_INFO5, "BatchReactor") << name()
                                           << " just received an order"
                                           << " for " << qty
                                           << " of " << out_commodity_;
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::phase(Phase p) {
  switch (p) {
    case PROCESS:
      start_time(context()->time());
  }
  phase_ = p;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::Refuel_() {
  while(n_core() < n_batches() && BatchIn(reserves_, batch_size())) {
    MoveBatchIn_();
  }
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::MoveBatchIn_() {
  core_.Push(reserves_.Pop());
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::MoveBatchOut_() {
  using cyclus::Material;
  Material::Ptr mat = core_.Pop();
  mat->Transmute(context()->GetRecipe(out_recipe()));
  storage_.Push(mat);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
cyclus::RequestPortfolio<Material>::Ptr BatchReactor::GetOrder_(double size) {
  using cyclus::CapacityConstraint;
  using cyclus::Material;
  using cyclus::RequestPortfolio;
  using cyclus::Request;

  Material::Ptr mat =
      Material::CreateUntracked(size, context()->GetRecipe(in_recipe_));
  
  RequestPortfolio<Material>::Ptr port(new RequestPortfolio<Material>());
  port->AddRequest(mat, this, in_commodity_);

  CapacityConstraint<Material> cc(size);
  port->AddConstraint(cc);

  return port;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::AddBatches_(cyclus::Material::Ptr mat) {
  using cyclus::Material;

  Material::Ptr last = reserves_.PopBack();
  if (last->quantity() < batch_size()) {
    if (last->quantity() + mat->quantity() <= batch_size()) {
      last->Absorb(mat);
      reserves.Push(last);
      return; // return because mat has been absorbed
    } else {
      Material::Ptr last_bit = mat->Extract(batch_size() - last->quantity());
      last->Absorb(last_bit);
    } // end if (last->quantity() + mat->quantity() <= batch_size())
  } // end if (last->quantity() < batch_size())
  reserves_.Push(last);
  
  while (mat->quantity() > batch_size()) {
    Material::Ptr batch = mat->Extract(batch_size());
    reserves_.Push(batch);
  }
  
  if (mat->quantity() > cyclus::eps_rsrc()) reserves_.Push(mat);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BatchReactor::SetUpPhaseNames_() {
  phase_names_.insert(std::make_pair(INITIAL, "initialization"));
  phase_names_.insert(std::make_pair(PROCESS, "processing batch(es)"));
  phase_names_.insert(std::make_pair(WAITING, "waiting for fuel"));
}

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::HandleTick(int time) {
//   using std::string;
//   using boost::lexical_cast;
//   LOG(cyclus::LEV_INFO3, "BReact") << name() << " is ticking at time "
//                                    << time << " {";
//   LOG(cyclus::LEV_DEBUG3, "BReact") << "The current phase is: "
//                                     << phase_names_[phase_];


//   if (LifetimeReached(time)) {
//     SetPhase(END);
//   }

//   double fuel_quantity, request;
//   string msg;

//   switch (phase()) {
//     case INIT:
//       // intentional fall through

//     case OPERATION:
//       cycle_timer_++;
//       break;

//     case REFUEL:
//       OffloadBatch();

//     case REFUEL_DELAY:
//       // intentional fall through

//     case WAITING:
//       // intentional fall through

//     case BEGIN:
//       cycle_timer_++;
//       fuel_quantity = preCore_.quantity() + inCore_.quantity();
//       request = in_core_loading() - fuel_quantity;
//       if (request > cyclus::eps()) {
//         MakeRequest(request);
//       }
//       break;

//     case END:
//       OffloadCore();
//       break;

//     default:
//       msg = "BatchReactors have undefined behvaior during ticks for phase: "
//             + phase_names_[phase_];
//       throw cyclus::Error(msg);
//       break;
//   }

//   MakeOffers();

//   LOG(cyclus::LEV_INFO3, "BReact") << "}";
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::HandleTock(int time) {
//   using std::string;
//   using boost::lexical_cast;
//   LOG(cyclus::LEV_INFO3, "BReact") << name() << " is tocking {";
//   LOG(cyclus::LEV_DEBUG3, "BReact") << "The current phase is: "
//                                     << phase_names_[phase_];

//   HandleOrders();

//   string msg;

//   switch (phase()) {

//     case END:
//       // if ( postCore_.empty() )
//       //   Decommission();
//       break;

//     case BEGIN:
//       // intentional fall through

//     case WAITING:
//       LoadCore();
//       if (CoreFilled()) {

//         SetPhase(OPERATION);
//         recycle_timer();
//       } else {
//         SetPhase(WAITING);
//       }
//       break;

//     case REFUEL:
//       SetPhase(REFUEL_DELAY);
//       time_delayed_ = 0;
//     case REFUEL_DELAY:
//       LoadCore();
//       if (time_delayed_ > refuel_delay() && CoreFilled()) {
//         SetPhase(OPERATION);
//         recycle_timer();
//       } else {
//         ++time_delayed_;
//       }
//       break;

//     case OPERATION:
//       if (CycleComplete()) {
//         SetPhase(REFUEL);
//       }
//       break;

//     default:
//       msg = "BatchReactors have undefined behvaior during tocks for phase: "
//             + phase_names_[phase_];
//       throw cyclus::Error(msg);
//       break;
//   }

//   LOG(cyclus::LEV_DEBUG3, "BReact") << "cycle timer: "
//                                     << cycle_timer_;
//   LOG(cyclus::LEV_DEBUG3, "BReact") << "delay: "
//                                     << time_delayed_;
//   LOG(cyclus::LEV_INFO3, "BReact") << "}";
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::ReceiveMessage(cyclus::Message::Ptr msg) {
//   // is this a message from on high?
//   if (msg->trans().supplier() == this) {
//     // file the order
//     ordersWaiting_.push_front(msg);
//     LOG(cyclus::LEV_INFO5, "BReact") << name() << " just received an order.";
//   } else {
//     throw cyclus::Error("BatchReactor is not the supplier of this msg.");
//   }
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::SendMessage(Communicator* recipient,
//                                cyclus::Transaction trans) {
//   cyclus::Message::Ptr msg(new cyclus::Message(this, recipient, trans));
//   msg->SendOn();
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// std::vector<cyclus::Resource::Ptr> BatchReactor::RemoveResource(
//   cyclus::Transaction order) {
//   cyclus::Transaction trans = order;
//   double amt = trans.resource()->quantity();

//   LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name() << " removed "
//                                     << amt << " of " << postCore_.quantity()
//                                     << " to its postcore buffer.";

//   return postCore_.PopQty(amt);
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::AddResource(cyclus::Transaction trans,
//                                std::vector<cyclus::Resource::Ptr> manifest) {
//   double preQuantity = preCore_.quantity();
//   preCore_.PushAll(manifest);
//   double added = preCore_.quantity() - preQuantity;
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name() << " added "
//                                     << added << " to its precore buffer.";
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::cycle_length(int time) {
//   cycle_length_ = time;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// int BatchReactor::cycle_length() {
//   return cycle_length_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::refuel_delay(int time) {
//   refuel_delay_ = time;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// int BatchReactor::refuel_delay() {
//   return refuel_delay_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::in_core_loading(double size) {
//   in_core_loading_ = size;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// double BatchReactor::in_core_loading() {
//   return in_core_loading_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::out_core_loading(double size) {
//   out_core_loading_ = size;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// double BatchReactor::out_core_loading() {
//   return out_core_loading_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::batches_per_core(int n) {
//   batches_per_core_ = n;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// int BatchReactor::batches_per_core() {
//   return batches_per_core_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// double BatchReactor::BatchLoading() {
//   return in_core_loading_ / batches_per_core_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::in_commodity(std::string name) {
//   in_commodity_ = name;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// std::string BatchReactor::in_commodity() {
//   return in_commodity_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::in_recipe(std::string name) {
//   in_recipe_ = name;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// std::string BatchReactor::in_recipe() {
//   return in_recipe_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::out_commodity(std::string name) {
//   out_commodity_ = name;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// std::string BatchReactor::out_commodity() {
//   return out_commodity_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::out_recipe(std::string name) {
//   out_recipe_ = name;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// std::string BatchReactor::out_recipe() {
//   return out_recipe_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Phase BatchReactor::phase() {
//   return phase_;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// bool BatchReactor::CheckDecommissionCondition() {
//   bool empty = (preCore_.empty() && inCore_.empty() &&
//                 postCore_.empty());
//   // if (!empty) {
//   //   string msg = "Can't delete a BatchReactor with material still in its inventory.";
//   //   throw cyclus::CycBatchReactorDestructException(msg);
//   // }
//   return empty;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::SetPhase(Phase p) {
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name()
//                                     << " is changing phases -";
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "  * from phase: " << phase_names_[phase_];
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "  * to phase: " << phase_names_[p];
//   phase_ = p;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::recycle_timer() {
//   cycle_timer_ = 1;
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// bool BatchReactor::CycleComplete() {
//   return (cycle_timer_ >= cycle_length_ - 1);
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// bool BatchReactor::CoreFilled() {
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "Querying whether the core is filled -";
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "  * quantity in core: " <<
//                                     inCore_.quantity();
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "  * core capacity: " <<
//                                     inCore_.capacity();
//   // @MJGFlag need to assert that the in core capacity must be > 0
//   // 9/29/12 error with a negative in core capacity
//   return (abs(inCore_.quantity() - inCore_.capacity()) < cyclus::eps());
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::MakeRequest(double amt) {
//   interactWithMarket(in_commodity(), amt, cyclus::REQUEST);
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::MakeOffers() {
//   if (!postCore_.empty()) {
//     interactWithMarket(out_commodity(), postCore_.quantity(), cyclus::OFFER);
//   }
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::interactWithMarket(std::string commod, double amt,
//                                       cyclus::TransType type) {
//   using std::string;
//   using cyclus::Context;
//   using cyclus::GenericResource;
//   using cyclus::Material;
//   using cyclus::Model;
  
//   LOG(cyclus::LEV_INFO4, "BReact") << " making requests {";
//   // get the market
//   cyclus::MarketModel* market = cyclus::MarketModel::MarketForCommod(commod);
//   Communicator* recipient = dynamic_cast<Communicator*>(market);
//   // set the price
//   double commodity_price = 0;
//   // request a generic resource
//   // build the transaction and message
//   cyclus::Transaction trans(this, type);
//   trans.SetCommod(commod);
//   trans.SetMinFrac(1.0);
//   trans.SetPrice(commodity_price);

//   if (type == cyclus::OFFER) {
//     GenericResource::Ptr trade_res =
//         GenericResource::CreateUntracked(amt, "kg", commod);
//     trans.SetResource(trade_res);
//   } else {
//     Material::Ptr trade_res =
//         Material::CreateUntracked(amt, context()->GetRecipe(in_recipe_));
//     trans.SetResource(trade_res);

//     LOG(cyclus::LEV_DEBUG1, "BatR") << "Requesting material: ";
//   }

//   // log the event
//   string text;
//   if (type == cyclus::OFFER) {
//     text = " has offered ";
//   } else {
//     text = " has requested ";
//   }
//   LOG(cyclus::LEV_INFO5, "BReact") << name() << text << amt
//                                    << " kg of " << commod << ".";
//   // send the message
//   SendMessage(recipient, trans);
//   LOG(cyclus::LEV_INFO4, "BReact") << "}";
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::HandleOrders() {
//   while (!ordersWaiting_.empty()) {
//     cyclus::Message::Ptr order = ordersWaiting_.front();
//     order->trans().ApproveTransfer();
//     ordersWaiting_.pop_front();
//   }
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::OffloadBatch() {
//   using cyclus::Context;
//   using cyclus::Material;
  
//   Material::Ptr m = cyclus::ResCast<Material>(inCore_.Pop());
//   double factor = out_core_loading() / in_core_loading();
//   double loss = m->quantity() * (1 - factor);

//   m->ExtractQty(loss); // mass discrepancy
//   m->Transmute(context()->GetRecipe(out_recipe()));
//   postCore_.Push(m);
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::LoadCore() {
//   using cyclus::Material;
//   if (preCore_.quantity() >= BatchLoading()) {
//     // combine materials into a single resource of batch size and move to inCore buf
//     std::vector<Material::Ptr> mats = cyclus::ResCast<Material>(preCore_.PopQty(BatchLoading()));
//     Material::Ptr m = mats[0];
//     for (int i = 1; i < mats.size(); ++i) {
//       m->Absorb(mats[i]);
//     }
//     inCore_.Push(m);
//     LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name()
//                                       << " moved fuel into the core:";
//     LOG(cyclus::LEV_DEBUG2, "BReact") << "  precore level: " << preCore_.quantity();
//     LOG(cyclus::LEV_DEBUG2, "BReact") << "  incore level: " << inCore_.quantity();
//   }
// }

// //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// void BatchReactor::OffloadCore() {
//   while (!inCore_.empty()) {
//     OffloadBatch();
//   }
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "BatchReactor " << name()
//                                     << " removed a core of fuel from the core:";
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "  precore level: " << preCore_.quantity();
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "  incore level: " << inCore_.quantity();
//   LOG(cyclus::LEV_DEBUG2, "BReact") << "  postcore level: " <<
//                                     postCore_.quantity();
// }

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
extern "C" cyclus::Model* ConstructBatchReactor(cyclus::Context* ctx) {
  return new BatchReactor(ctx);
}

} // namespace cycamore
