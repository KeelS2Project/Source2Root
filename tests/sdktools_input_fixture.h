// Included after the SDKTools service fixture in its anonymous namespace.
void Inputs()
{
    Fixture f; auto service = f.ServiceFor(1,true);
    auto entity = service->Find(4), participant = service->Find(3);
    bool invoked{};
    KeelEntityInputValue value{}; value.size = sizeof(value);
    Check(service->InputCapabilities() == std::array<unsigned,2>{511,383},"direct and queue capabilities");
    for (unsigned type = 0; type <= KEELS2_INPUT_ENTITY; ++type) {
        value.type = type; value.int_value = 1; value.float_value = 2.5f; value.string_value = "typed";
        value.vector_value[1] = 7; value.color_value[3] = 255;
        const auto* payload = type == KEELS2_INPUT_ENTITY ? participant.get() : nullptr;
        entity->Input("Enable",value,invoked,participant.get(),entity.get(),payload);
        Check(invoked && f.last_input.value.type == type && f.last_input.activator && f.last_input.caller &&
            f.last_input.value_entity == (payload ? f.last_input.activator : 0),"typed input and participant identity");
        if (type == KEELS2_INPUT_COLOR) {
            Reject([&] { entity->Input("Enable",value,invoked,nullptr,nullptr,nullptr,true,1.25f); },"queued color refused");
            Check(!invoked,"unsupported color did not invoke");
        } else {
            entity->Input("Enable",value,invoked,nullptr,nullptr,payload,true,1.25f);
            Check(invoked && f.last_input.queued && f.last_input.delay == 1.25f,"queue flag and delay");
        }
    }
    value.type = KEELS2_INPUT_VOID;
    const auto invalid = [&](auto action) { invoked = true; Reject(action,"invalid input rejected"); Check(!invoked,"invalid input clears invocation"); };
    invalid([&] { entity->Input("",value,invoked); });
    invalid([&] { entity->Input("bad\nname",value,invoked); });
    invalid([&] { entity->Input("Enable",value,invoked,nullptr,nullptr,nullptr,false,1); });
    invalid([&] { entity->Input("Enable",value,invoked,nullptr,nullptr,nullptr,true,-1); });
    invalid([&] { entity->Input("Enable",value,invoked,nullptr,nullptr,participant.get()); });
    value.type = KEELS2_INPUT_ENTITY;
    invalid([&] { entity->Input("Enable",value,invoked); }); value.type = KEELS2_INPUT_VOID;
    auto foreign_service = f.ServiceFor(2); auto foreign = foreign_service->Find(3);
    invalid([&] { entity->Input("Enable",value,invoked,foreign.get()); }); foreign.reset(); foreign_service.reset();
    auto pending = service->Create("prop_dynamic");
    invalid([&] { entity->Input("Enable",value,invoked,pending.get()); });
    invalid([&] { pending->Input("Enable",value,invoked); }); pending.reset();
    ++f.epoch; invalid([&] { entity->Input("Enable",value,invoked); }); --f.epoch;
    std::thread worker([&] { invalid([&] { entity->Input("Enable",value,invoked); }); }); worker.join();
    char name[] = "Enable", text[] = "original";
    value.type = KEELS2_INPUT_STRING; value.string_value = text;
    f.on_input = [&] { name[0] = text[0] = 'X'; value.type = KEELS2_INPUT_VOID; };
    entity->Input(name,value,invoked); f.on_input = {};
    Check(invoked && f.input_name == "Enable" && f.input_text == "original","source mutation cannot change input snapshot");
    f.input_result = KEEL_RESULT_ENGINE_FAILURE; f.input_invoked = KEEL_FALSE;
    invalid([&] { entity->Input("Enable",value,invoked); });
    f.input_invoked = KEEL_TRUE;
    Reject([&] { entity->Input("Enable",value,invoked); },"invoked input failure propagated"); Check(invoked,"invocation survives engine failure");
    f.input_result = KEEL_RESULT_OK; f.input_invoked = 7;
    Reject([&] { entity->Input("Enable",value,invoked); },"invalid invocation marker rejected"); Check(invoked,"unknown nonzero invocation is conservatively true");
    f.input_invoked = KEEL_TRUE;
    unsigned depth{}, visits{};
    f.on_input = [&] {
        ++depth; ++visits; bool nested{};
        if (depth == 8) Reject([&] { entity->Input("Enable",value,nested); },"input recursion bound");
        else entity->Input("Enable",value,nested);
        --depth;
    };
    entity->Input("Enable",value,invoked); f.on_input = {};
    Check(visits == 8 && invoked,"eight active inputs allowed");
    f.on_input_caps = [&] {
        ++depth; ++visits;
        if (depth == 8) Reject([&] { service->InputCapabilities(); },"capability recursion bound");
        else service->InputCapabilities();
        --depth;
    };
    visits = 0; service->InputCapabilities(); f.on_input_caps = {};
    Check(visits == 8,"eight capability queries allowed");
    f.input_direct |= 1u << 9; Reject([&] { service->InputCapabilities(); },"unknown capability rejected"); f.input_direct = 511;
    // Describing the target may close another captured participant. No later
    // dereference of that destroyed C++ object is permitted.
    f.on_describe = [&] { participant.reset(); };
    invalid([&] { entity->Input("Enable",value,invoked,participant.get()); }); f.on_describe = {};
    participant = service->Find(3);
    const std::weak_ptr<Service> weak = service;
    f.on_input = [&] { entity.reset(); participant.reset(); service.reset(); };
    entity->Input("Enable",value,invoked,participant.get()); f.on_input = {};
    Check(invoked && weak.expired() && f.entities.empty(),"input retains provider until callbacks and then releases all resources");
    auto legacy = std::make_shared<Service>(1,Fixture::entity_api,Fixture::schema_api,Fixture::player_api,Fixture::runtime_api);
    entity = legacy->Find(4);
    invalid([&] { entity->Input("Enable",value,invoked); });
    Reject([&] { legacy->InputCapabilities(); },"optional input absence supported");
    auto bad = Fixture::input_api; bad.dispatch = nullptr;
    Reject([&] { static_cast<void>(std::make_shared<Service>(1,Fixture::entity_api,Fixture::schema_api,Fixture::player_api,Fixture::runtime_api,
        nullptr,nullptr,nullptr,&bad)); },"incomplete optional input table refused");
}
