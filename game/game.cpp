//
// open horizon -- undefined_darkness@outlook.com
//

#include "game.h"
#include "math/scalar.h"
#include "util/util.h"
#include "util/xml.h"
#include <algorithm>
#include <time.h>

namespace game
{
//------------------------------------------------------------

namespace { const float meps_to_kmph = 3.6f; }

//------------------------------------------------------------

namespace { const static params::text_params &get_arms_param() { static params::text_params ap("Arms/ArmsParam.txt"); return ap; } }

//------------------------------------------------------------

class weapon_information
{
public:
    struct weapon
    {
        std::string id;
        std::string model;
        int count;
    };

    struct aircraft
    {
        std::wstring name;
        std::string role;
        weapon missile;
        std::vector<weapon> special;
    };

public:
    static weapon_information &get()
    {
        static weapon_information info("aircrafts.xml");
        return info;
    }

    aircraft *get_aircraft_weapons(const char *name)
    {
        if (!name)
            return 0;

        std::string name_str(name);
        std::transform(name_str.begin(), name_str.end(), name_str.begin(), ::tolower);
        auto a = m_aircrafts.find(name);
        if (a == m_aircrafts.end())
            return 0;

        return &a->second;
    }

    const std::vector<std::string> &get_aircraft_ids(const std::string &role)
    {
        for (auto &l: m_lists)
        {
            if (l.first == role)
                return l.second;
        }

        const static std::vector<std::string> empty;
        return empty;
    }

private:
    weapon_information(const char *xml_name)
    {
        pugi::xml_document doc;
        if (!load_xml(xml_name, doc))
            return;

        pugi::xml_node root = doc.child("aircrafts");
        if (!root)
            return;

        for (pugi::xml_node ac = root.child("aircraft"); ac; ac = ac.next_sibling("aircraft"))
        {
            auto id = ac.attribute("id").as_string("");
            aircraft &a = m_aircrafts[id];
            a.role = ac.attribute("role").as_string("");
            if (!a.role.empty())
                aircraft_ids(a.role).push_back(id);
            const std::string name = ac.attribute("name").as_string("");
            a.name = std::wstring(name.begin(), name.end());

            for (pugi::xml_node wpn = ac.first_child(); wpn; wpn = wpn.next_sibling())
            {
                weapon w;
                w.id = wpn.attribute("id").as_string("");
                w.model = wpn.attribute("model").as_string("");
                w.count = wpn.attribute("count").as_int(0);

                std::string name(wpn.name() ? wpn.name() : "");
                if (name == "msl")
                    a.missile = w;
                else if (name == "spc")
                    a.special.push_back(w);
            }
        }
    }

    std::vector<std::string> &aircraft_ids(const std::string &role)
    {
        for (auto &l: m_lists)
        {
            if (l.first == role)
                return l.second;
        }

        m_lists.push_back({role, {}});
        return m_lists.back().second;
    }


    std::map<std::string, aircraft> m_aircrafts;
    std::vector<std::pair<std::string, std::vector<std::string> > > m_lists;
};

//------------------------------------------------------------

wpn_params::wpn_params(std::string id, std::string model)
{
    const std::string action = "." + id + ".action.";
    const std::string lockon = "." + id + ".lockon.";
    const std::string lockon_dfm = "." + id + ".lockon_dfm.";

    this->id = id;
    this->model = model;

    auto &param = get_arms_param();

    lockon_range = param.get_float(lockon + "range");
    lockon_time = param.get_int(lockon_dfm + "timeLockon") * 1000;
    lockon_count = param.get_float(lockon + "lockonNum");
    lockon_air = param.get_int(lockon + "target_air") > 0;
    lockon_ground = param.get_int(lockon + "target_grd") > 0;
    action_range = param.get_int(action + "range");
    reload_time = param.get_float(lockon + "reload") * 1000;

    float lon_angle = param.get_float(lockon + "angle") * 0.5f * nya_math::constants::pi / 180.0f;
    //I dunno
    if (id == "SAAM")
        lon_angle *= 0.3f;
    lockon_angle_cos = cosf(lon_angle);
}

//------------------------------------------------------------

std::vector<std::string> get_aircraft_ids(const std::vector<std::string> &roles)
{
    std::vector<std::string> planes;
    for (auto &r: roles)
    {
        auto &l = weapon_information::get().get_aircraft_ids(r);
        planes.insert(planes.end(), l.begin(), l.end());
    }

    return planes;
}

//------------------------------------------------------------

const std::wstring &get_aircraft_name(const std::string &id)
{
    auto *a = weapon_information::get().get_aircraft_weapons(id.c_str());
    if(!a)
    {
        static std::wstring invalid;
        return invalid;
    }

    return a->name;
}

//------------------------------------------------------------

missile_ptr world::add_missile(const char *id, const char *model_id)
{
    if (!model_id || !id)
        return missile_ptr();

    renderer::model m;
    m.load((std::string("w_") + model_id).c_str(), m_render_world.get_location_params());
    return add_missile(id, m);
}

//------------------------------------------------------------

missile_ptr world::add_missile(const char *id, const renderer::model &mdl)
{
    if (!id)
        return missile_ptr();

    missile_ptr m(new missile());
    m->phys = m_phys_world.add_missile(id);
    m->render = m_render_world.add_missile(mdl);

    auto &param = get_arms_param();

    const std::string pref = "." + std::string(id) + ".action.";
    m->time = param.get_float(pref + "endTime") * 1000;
    m->homing_angle_cos = cosf(param.get_float(".MISSILE.action.hormingAng"));

    m_missiles.push_back(m);
    return m;
}

//------------------------------------------------------------

plane_ptr world::add_plane(const char *name, int color, bool player, net_plane_ptr ptr)
{
    if (!name)
        return plane_ptr();

    plane_ptr p(new plane());
    const bool add_to_world = !(ptr && !ptr->source);
    p->phys = m_phys_world.add_plane(name, add_to_world);

    p->render = m_render_world.add_aircraft(name, color, player);
    p->phys->nose_offset = p->render->get_bone_pos("clv1");

    p->hp = p->max_hp = int(p->phys->params.misc.maxHp);

    p->hit_radius = name[0] == 'b' ? 12.0f : 7.0f;

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    p->render->set_time(tm_now->tm_sec + tm_now->tm_min * 60 + tm_now->tm_hour * 60 * 60); //ToDo

    auto wi = weapon_information::get().get_aircraft_weapons(name);
    if (wi)
    {
        if (!wi->special.empty())
        {
            p->render->load_special(wi->special[0].model.c_str(), m_render_world.get_location_params());
            p->special = wpn_params(wi->special[0].id, wi->special[0].model);
            p->special_max_count = wi->special[0].count;
        }

        p->render->load_missile(wi->missile.model.c_str(), m_render_world.get_location_params());
        p->missile = wpn_params(wi->missile.id, wi->missile.model);
        p->missile_max_count = wi->missile.count;
    }

    p->missile_count = p->missile_max_count;
    p->special_count = p->special_max_count;

    if (player)
    {
        m_hud.load(name, m_render_world.get_location_name());
        m_hud.set_missiles(p->missile.id.c_str(), 0);
        m_hud.set_missiles_count(p->missile_count);
        m_player = p;
    }

    if (m_network)
        p->net = ptr ? ptr : m_network->add_plane(name, color);

    m_planes.push_back(p);
    get_arms_param(); //cache
    return p;
}

//------------------------------------------------------------

void world::spawn_explosion(const nya_math::vec3 &pos, int damage, float radius)
{
    //ToDo: damage

    m_render_world.spawn_explosion(pos, radius);
}

//------------------------------------------------------------

inline bool line_sphere_intersect(const vec3 &start, const vec3 &end, const vec3 &sp_center, const float sp_radius)
{
    const vec3 diff = end - start;
    const float len = nya_math::clamp(diff.dot(sp_center - start) / diff.length_sq(), 0.0f, 1.0f);
    const vec3 inter_pt = start + len  * diff;
    return (inter_pt - sp_center).length_sq() <= sp_radius * sp_radius;
}

//------------------------------------------------------------

void world::spawn_bullet(const char *type, const vec3 &pos, const vec3 &dir, const plane_ptr &owner)
{
    vec3 r;
    const bool hit_world = m_phys_world.spawn_bullet(type, pos, dir, r);
    for (auto &p: m_planes)
    {
        if (p->hp <= 0 || is_ally(owner, p))
            continue;

        if (line_sphere_intersect(pos, r, p->get_pos(), p->hit_radius))
        {
            p->take_damage(60, *this);
            if (p->hp <= 0)
                on_kill(owner, p);
        }
    }

    if (hit_world)
    {
        //ToDo: spark
    }
}

//------------------------------------------------------------

plane_ptr world::get_plane(int idx)
{
    if (idx < 0 || idx >= (int)m_planes.size())
        return plane_ptr();

    return m_planes[idx];
}

//------------------------------------------------------------

missile_ptr world::get_missile(int idx)
{
    if (idx < 0 || idx >= (int)m_planes.size())
        return missile_ptr();

    return m_missiles[idx];
}

//------------------------------------------------------------

void world::set_location(const char *name)
{
    m_render_world.set_location(name);
    m_phys_world.set_location(name);

    m_hud.set_location(name);
}

//------------------------------------------------------------

void world::update(int dt)
{
    if (m_network)
        m_network->update();

    m_planes.erase(std::remove_if(m_planes.begin(), m_planes.end(), [](const plane_ptr &p)
                                  { return p.use_count() <= 1 && !p->net; }), m_planes.end());
    m_missiles.erase(std::remove_if(m_missiles.begin(), m_missiles.end(), [](const missile_ptr &m)
                                    { return m.use_count() <= 1 && m->time <= 0; }), m_missiles.end());
    for (auto &p: m_planes)
        p->phys->controls = p->controls;

    m_phys_world.update_planes(dt, [this](const phys::object_ptr &a, const phys::object_ptr &b)
    {
        auto p = this->get_plane(a);

        if (!b) //hit ground
        {
            if (p->hp > 0)
            {
                p->take_damage(9000, *this);
                on_kill(plane_ptr(), p);
                p->render->set_hide(true);
            }
        }
    });

    for (auto &m: m_missiles)
        m->update_homing(dt);

    m_phys_world.update_missiles(dt, [this](const phys::object_ptr &a, const phys::object_ptr &b)
    {
        auto m = this->get_missile(a);
        if (m && m->time > 0)
        {
            this->spawn_explosion(m->phys->pos, 0, 10.0);
            m->time = 0;
        }
    });

    m_phys_world.update_bullets(dt);

    for (auto &p: m_planes)
        p->update(dt, *this);

    if (!m_player.expired())
        m_player.lock()->update_hud(*this, m_hud);

    for (auto &m: m_missiles)
        m->update(dt, *this);

    const auto &bullets_from = m_phys_world.get_bullets();
    auto &bullets_to = m_render_world.get_bullets();
    bullets_to.clear();
    for (auto &b: bullets_from)
        bullets_to.add_bullet(b.pos, b.vel);

    m_render_world.update(dt);

    if (m_network)
    {
        m_network->update_post(dt);

        network_interface::msg_add_plane m;
        while(m_network->get_add_plane_msg(m))
            add_plane(m.name.c_str(), m.color, false, m_network->add_plane(m));
    }
}

//------------------------------------------------------------

bool world::is_ally(const plane_ptr &a, const plane_ptr &b)
{
    if (a == b)
        return true;

    if (!m_ally_handler)
        return false;

    return m_ally_handler(a, b);
}

//------------------------------------------------------------

void world::on_kill(const plane_ptr &k, const plane_ptr &v)
{
    if (m_on_kill_handler)
        m_on_kill_handler(k, v);
}

//------------------------------------------------------------

plane_ptr world::get_plane(const phys::object_ptr &o)
{
    for (auto &p: m_planes)
    {
        if (p->phys == o)
            return p;
    }

    return plane_ptr();
}

//------------------------------------------------------------

missile_ptr world::get_missile(const phys::object_ptr &o)
{
    for (auto &m: m_missiles)
    {
        if (m->phys == o)
            return m;
    }

    return missile_ptr();
}

//------------------------------------------------------------

void plane::reset_state()
{
    hp = max_hp;
    targets.clear();

    phys->reset_state();

    render->set_dead(false);

    //render->reset_state(); //ToDo

    render->set_special_visible(-1, true);

    need_fire_missile = false;
    missile_bay_time = 0;

    for (auto &c: missile_cooldown) c = 0;
    missile_mount_cooldown.clear();
    missile_mount_idx = 0;

    for (auto &c: special_cooldown) c = 0;
    special_mount_cooldown.clear();
    special_mount_idx = 0;
    jammed = false;
}

//------------------------------------------------------------

void plane::select_target(const object_ptr &o)
{
    for (auto &t: targets)
    {
        if (std::static_pointer_cast<object>(t.target_plane.lock()) != o)
            continue;

        std::swap(t, targets.front());
        return;
    }
}

//------------------------------------------------------------

void plane::update_targets(world &w)
{
    const plane_ptr &me = shared_from_this();
    jammed = false;

    //ToDo

    for (int i = 0; i < w.get_planes_count(); ++i)
    {
        auto p = w.get_plane(i);
        if (me == p)
            continue;

        if (p->hp <= 0)
            continue;

        if (!w.is_ally(me, p))
        {
            auto target_dir = p->get_pos() - me->get_pos();
            const float dist = target_dir.length();
            auto fp = std::find_if(targets.begin(), targets.end(), [p](target_lock &t){ return p == t.target_plane.lock(); });
            if (dist < 12000.0f) //ToDo
            {
                if (p->is_ecm_active() && dist < special.action_range)
                {
                    jammed = true;
                    targets.clear();
                    break;
                }

                if (fp == targets.end())
                    fp = targets.insert(targets.end(), {p, false});

                if (special_weapon_selected)
                {
                    if (!fp->locked)
                        continue;

                    int count = fp->locked;

                    if (dist > special.lockon_range)
                        fp->locked = 0;
                    else
                    {
                        const float c = target_dir.dot(me->get_rot().rotate(nya_math::vec3(0.0, 0.0, 1.0))) / dist;
                        if (c < special.lockon_angle_cos)
                            fp->locked = 0;
                    }

                    //ToDo: somewhere else
/*
                    if (player)
                    {
                        if (is_Xaam)
                        {
                            int lockon_count = 0;
                            for (auto &t: targets)
                                lockon_count += t.locked;

                            for (int i = 0; i < count; ++i)
                                h.set_lock(i + lockon_count, false, true);
                        }
                        else
                        {
                            const bool ready0 = special_cooldown[0] <=0;
                            const bool ready1 = special_cooldown[1] <=0;

                            if (ready0 || ready1)
                                h.set_lock(ready0 ? 0 : 1, false, true);
                        }
                    }
*/
                }
                else
                {
                    if (dist > missile.lockon_range)
                        fp->locked = false;
                    else
                    {
                        const float c = target_dir.dot(me->get_rot().rotate(nya_math::vec3(0.0, 0.0, 1.0))) / dist;
                        if (c < missile.lockon_angle_cos)
                            fp->locked = 0;
                        else if (fp != targets.begin())
                            fp->locked = 0;
                        else
                            fp->locked = 1;
                    }
                }
            }
            else if (fp != targets.end())
                fp = targets.erase(fp);
        }
    }

    targets.erase(std::remove_if(targets.begin(), targets.end(), [](target_lock &t){ return t.target_plane.expired()
        || t.target_plane.lock()->hp <= 0; }), targets.end());
}

//------------------------------------------------------------

void plane::update_render()
{
    render->set_hide(hp <= 0);
    if (hp <= 0)
        return;

    render->set_pos(phys->pos);
    render->set_rot(phys->rot);

    render->set_damage(max_hp ? float(max_hp-hp) / max_hp : 0.0);

    const float speed = phys->vel.length() * meps_to_kmph;
    const float speed_k = nya_math::max((phys->params.move.speed.speedMax - speed) / phys->params.move.speed.speedMax, 0.1f);

    render->set_speed(speed);

    const float el = nya_math::clamp(-controls.rot.z - controls.rot.x, -1.0f, 1.0f) * speed_k;
    const float er = nya_math::clamp(controls.rot.z - controls.rot.x, -1.0f, 1.0f) * speed_k;
    render->set_elev(el, er);

    const float rl = nya_math::clamp(-controls.rot.y + controls.brake, -1.0f, 1.0f) * speed_k;
    const float rr = nya_math::clamp(-controls.rot.y - controls.brake, -1.0f, 1.0f) * speed_k;
    render->set_rudder(rl, rr, -controls.rot.y);

    render->set_aileron(-controls.rot.z * speed_k, controls.rot.z * speed_k);
    render->set_canard(controls.rot.x * speed_k);
    render->set_brake(controls.brake);
    render->set_flaperon(speed < phys->params.move.speed.speedCruising - 100 ? -1.0 : 1.0);
    render->set_wing_sweep(speed >  phys->params.move.speed.speedCruising + 250 ? 1.0 : -1.0);

    render->set_intake_ramp(phys->thrust_time >= phys->params.move.accel.thrustMinWait ? 1.0 : -1.0);

    const float aoa = acosf(nya_math::vec3::dot(nya_math::vec3::normalize(phys->vel), get_dir()));
    render->set_aoa(aoa);

    render->set_missile_bay(missile_bay_time > 0);
    render->set_special_bay(special_weapon_selected);
    render->set_mgun_bay(controls.mgun);

    render->set_mgun_fire(is_mg_bay_ready() && controls.mgun);
    render->set_mgp_fire(special_weapon_selected && controls.missile && special.id == "MGP");
}

//------------------------------------------------------------

bool plane::is_mg_bay_ready()
{
    return render->is_mgun_ready();
}

//------------------------------------------------------------

bool plane::is_special_bay_ready()
{
    if (!render->has_special_bay())
        return true;

    return special_weapon_selected ? render->is_special_bay_opened() : render->is_special_bay_closed();
}

//------------------------------------------------------------

void plane::update(int dt, world &w)
{
    if (net)
    {
        if (net->source)
        {
            net->pos = phys->pos;
            net->rot = phys->rot;
            net->vel = phys->vel;
            net->ctrl_rot = controls.rot;
            net->ctrl_brake = controls.brake;
            net->hp = hp;
        }
        else
        {
            //float kdt = (long(w.get_net_time() + dt) - long(net->time)) * 0.001f;
            float kdt = dt * 0.001f;

            phys->pos = net->pos + net->vel * kdt;
            phys->rot = net->rot;
            phys->vel = net->vel;
            controls.rot = net->ctrl_rot;
            controls.brake = net->ctrl_brake;
            hp = net->hp;
        }
    }

    const plane_ptr &me = shared_from_this();
    const auto dir = get_dir();
    const auto pos_fix = phys->pos - render->get_pos(); //skeleton is not updated yet

    update_render();

    if (hp <= 0)
        return;

    phys->wing_offset = render->get_wing_offset();

    //switch weapons

    if (controls.change_weapon && controls.change_weapon != last_controls.change_weapon && is_special_bay_ready())
    {
        special_weapon_selected = !special_weapon_selected;

        if (!saam_missile.expired())
            saam_missile.lock()->target.reset();

        missile_bay_time = 0;
    }

    //mgun

    if (controls.mgun && render->is_mgun_ready())
    {
        mgun_fire_update += dt;
        const int mgun_update_time = 150;
        if (mgun_fire_update > mgun_update_time)
        {
            mgun_fire_update %= mgun_update_time;

            for (int i = 0; i < render->get_mguns_count(); ++i)
                w.spawn_bullet("MG", render->get_mgun_pos(i) + pos_fix, dir, me);
        }
    }

    //reload

    for (auto &m: missile_cooldown) if (m > 0) m -= dt;

    for (int i = 0; i < (int)missile_mount_cooldown.size(); ++i)
    {
        if (missile_mount_cooldown[i] < 0)
            continue;

        missile_mount_cooldown[i] -= dt;
        if (missile_mount_cooldown[i] < 0)
            render->set_missile_visible(i, true);
    }

    for (int i = 0; i < (int)special_mount_cooldown.size(); ++i)
    {
        if (special_mount_cooldown[i] < 0)
            continue;

        special_mount_cooldown[i] -= dt;
        if (special_mount_cooldown[i] < 0)
            render->set_special_visible(i, true);
    }

    //ecm

    if (is_ecm_active())
    {
        for (int i = 0; i < w.get_missiles_count(); ++i)
        {
            auto m = w.get_missile(i);
            if (!m)
                continue;

            if (!m->owner.expired() && w.is_ally(me, m->owner.lock()))
                continue;

            if ((get_pos() - m->phys->pos).length_sq() > special.action_range * special.action_range)
                continue;

            m->target.reset();
        }
    }

    //targets
    
    update_targets(w);

    //missiles

    if (controls.missile && !special_weapon_selected)
        missile_bay_time = 3000;

    if (missile_bay_time > 0)
        missile_bay_time -= dt;

/*
    int count = 1;
    if (!special.id.empty())
    {
        if (special.id[1] == '4')
            count = 4;
        else if (special.id[1] == '6')
            count = 6;
    }

    const auto dir = phys->rot.rotate(nya_math::vec3(0.0f, 0.0f, 1.0f));

    const bool is_mgp = special.id == "MGP";
    const bool is_ecm = special.id == "ECM";
    const bool is_qaam = special.id == "QAAM";
    const bool is_Xaam = special.lockon_count > 2;
    const bool is_saam = special.id == "SAAM";

    const int special_cooldown_time = is_ecm ? 9000 : 7000;

    if (controls.missile != last_controls.missile)
    {
        if (special_weapon_selected)
        {
            if (controls.missile)
            {
                if (!render->has_special_bay() || render->is_special_bay_opened())
                {
                    if ((special_cooldown[0] <=0 || special_cooldown[1] <= 0) && render->get_special_mount_count() > 0)
                    {
                        if (is_ecm)
                        {
                            if (special_cooldown[0] <= 0)
                            {
                                special_cooldown[0] = special_cooldown_time;
                                if (player)
                                    h.set_lock(0, false, false);
                            }
                        }
                        else if (special.id == "ADMM")
                        {
                            //ToDo
                        }
                        else if (special.id == "EML")
                        {
                            //ToDo
                        }
                        else
                        {
                            if (count == 1)
                            {
                                if (special_cooldown[0] <= 0)
                                {
                                    special_cooldown[0] = special_cooldown_time;
                                    if (player)
                                        h.set_lock(0, false, false);
                                }
                                else if (special_cooldown[1] <= 0)
                                {
                                    special_cooldown[1] = special_cooldown_time;
                                    if (player)
                                        h.set_lock(1, false, false);
                                }
                            }
                            else
                                special_cooldown[0] = special_cooldown[1] = special_cooldown_time;

                            special_mount_cooldown.resize(render->get_special_mount_count());

                            std::vector<w_ptr<plane> > locked_targets;
                            for (auto &t: targets)
                            {
                                for (int j = 0; j < t.locked; ++j)
                                    locked_targets.push_back(t.target_plane);
                                t.locked = 0;
                            }

                            for (int i = 0; i < count; ++i)
                            {
                                auto m = w.add_missile(special.id.c_str(), render->get_special_model());
                                m->owner = shared_from_this();
                                special_mount_idx = ++special_mount_idx % render->get_special_mount_count();
                                special_mount_cooldown[special_mount_idx] = special_cooldown_time;
                                render->set_special_visible(special_mount_idx, false);
                                m->phys->pos = render->get_special_mount_pos(special_mount_idx) + pos_fix;
                                m->phys->rot = render->get_special_mount_rot(special_mount_idx);
                                m->phys->vel = phys->vel;
                                m->phys->target_dir = m->phys->rot.rotate(vec3(0.0, 0.0, 1.0)); //ToDo

                                if (i < (int)locked_targets.size())
                                    m->target = locked_targets[i];

                                if (player && count > 1)
                                    h.set_lock(i, false, false);

                                if (is_saam)
                                {
                                    if (!saam_missile.expired())
                                        saam_missile.lock()->target.reset(); //one at a time
                                    saam_missile = m;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (need_fire_missile && render->is_missile_ready())
    {
        need_fire_missile = false;
        missile_mount_cooldown.resize(render->get_missile_mount_count());
        if ((missile_cooldown[0] <=0 || missile_cooldown[1] <= 0) && render->get_missile_mount_count() > 0)
        {
            if (missile_cooldown[0] <= 0)
                missile_cooldown[0] = missile_cooldown_time;
            else if (missile_cooldown[1] <= 0)
                missile_cooldown[1] = missile_cooldown_time;

            auto m = w.add_missile(missile.id.c_str(), render->get_missile_model());
            m->owner = shared_from_this();
            missile_mount_idx = ++missile_mount_idx % render->get_missile_mount_count();
            missile_mount_cooldown[missile_mount_idx] = missile_cooldown_time;
            render->set_missile_visible(missile_mount_idx, false);
            m->phys->pos = render->get_missile_mount_pos(missile_mount_idx) + pos_fix;
            m->phys->rot = render->get_missile_mount_rot(missile_mount_idx);
            m->phys->vel = phys->vel;

            m->phys->target_dir = m->phys->rot.rotate(vec3(0.0, 0.0, 1.0));
            if (!targets.empty() && targets.front().locked > 0)
                m->target = targets.front().target_plane;
        }
    }

    for (int i = 0; i < 2; ++i)
    {
        auto &s = special_cooldown[i];

        if (s > 0)
        {
            s -= dt;

            if (player && special_weapon_selected && s <= 0)
            {
                if (is_Xaam)
                {
                    if (i == 0)
                    {
                        for (int j = 0; j < (int)special_mount_cooldown.size(); ++j)
                            h.set_lock(j, false, true);
                    }
                }
                else
                    h.set_lock(i, false, true);
            }
        }
    }

    bool saam_locked = false;

    if (special_weapon_selected)
    {
        if (!targets.empty() && !targets.begin()->target_plane.expired())
        {
            if (is_saam)
            {
                auto p = targets.begin()->target_plane.lock();
                auto target_dir = p->get_pos() - me->get_pos();
                const float dist = target_dir.length();

                if (dist < special.lockon_range)
                {
                    const float c = target_dir.dot(me->get_rot().rotate(nya_math::vec3(0.0, 0.0, 1.0))) / dist;
                    saam_locked = c > special.lockon_angle_cos;
                }

                targets.begin()->locked = saam_locked ? 1 : 0;
            }
            else if (is_qaam)
            {
                const bool ready0 = special_cooldown[0] <=0;
                const bool ready1 = special_cooldown[1] <=0;

                if (ready0 || ready1)
                {
                    auto p = targets.begin()->target_plane.lock();
                    auto target_dir = p->get_pos() - me->get_pos();
                    const float dist = target_dir.length();

                    if (dist < special.lockon_range)
                    {
                        const float c = target_dir.dot(me->get_rot().rotate(nya_math::vec3(0.0, 0.0, 1.0))) / dist;
                        if (c > special.lockon_angle_cos)
                        {
                            lock_timer += dt;
                            if (lock_timer > special.lockon_reload)
                            {
                                lock_timer %= special.lockon_reload;

                                targets.begin()->locked = 1;

                                if (player)
                                    h.set_lock(ready0 ? 0 : 1, true, true);
                            }
                        }
                        else
                            lock_timer = 0;
                    }
                    else
                        lock_timer = 0;
                }
            }
        }
        else
            lock_timer = 0;

        if (!saam_missile.expired())
        {
            auto m = saam_missile.lock();
            if (saam_locked)
                m->target = targets.begin()->target_plane;
            else
                m->target.reset();
        }

        if (is_Xaam && special_cooldown[0] <= 0)
        {
            lock_timer += dt;
            if (lock_timer > special.lockon_reload)
            {
                lock_timer %= special.lockon_reload;

                int lockon_count = 0;
                for (auto &t: targets)
                    lockon_count += t.locked;

                if (lockon_count < special.lockon_count)
                {
                    for (int min_lock = 0; min_lock < special.lockon_count; ++min_lock)
                    {
                        bool found = false;
                        for (auto &t: targets)
                        {
                            if (t.locked > min_lock)
                                continue;

                            auto p = t.target_plane.lock();
                            auto target_dir = p->get_pos() - me->get_pos();
                            const float dist = target_dir.length();
                            if (dist > special.lockon_range)
                                continue;

                            const float c = target_dir.dot(me->get_rot().rotate(nya_math::vec3(0.0, 0.0, 1.0))) / dist;
                            if (c < special.lockon_angle_cos)
                                continue;

                            if (player)
                                h.set_lock(lockon_count, true, true);

                            ++t.locked;
                            found = true;
                            break;
                        }

                        if (found)
                            break;
                    }
                }
            }
        }

        if (is_mgp && controls.missile)
        {
            mgp_fire_update += dt;
            const int mgp_update_time = 150;
            if (mgp_fire_update > mgp_update_time)
            {
                mgp_fire_update %= mgp_update_time;

                for (int i = 0; i < render->get_special_mount_count(); ++i) //ToDo
                    w.spawn_bullet("MGP", render->get_special_mount_pos(i) + pos_fix, dir, me);
            }
        }
    }

    //cockpit animations and hud

    if (player) //ToDo
    {
        if (controls.change_target && controls.change_target != last_controls.change_target)
        {
            if (targets.size() > 1)
            {
                if (!is_Xaam)
                    targets.front().locked = 0;

                targets.push_back(targets.front());
                targets.pop_front();
            }
        }

        h.clear_targets();
        h.clear_ecm();

        for (int i = 0; i < w.get_missiles_count(); ++i)
        {
            auto m = w.get_missile(i);
            if (m)
                h.add_target(m->phys->pos, m->phys->rot.get_euler().y, gui::hud::target_missile, gui::hud::select_not);
        }

        bool hud_mgun = controls.mgun;

        for (int i = 0; i < w.get_planes_count(); ++i)
        {
            auto p = w.get_plane(i);

            if (p->is_ecm_active())
                h.add_ecm(p->get_pos());

            if (me == p)
                continue;

            auto select = gui::hud::select_not;
            auto target = gui::hud::target_air;

            if (w.is_ally(me, p))
            {
                if (p->hp <= 0)
                    continue;

                target = gui::hud::target_air_ally;
            }
            else
            {
                auto first_target = targets.begin();
                if (first_target != targets.end())
                {
                    if (p == first_target->target_plane.lock())
                    {
                        select = gui::hud::select_current;
                        const float gun_range = 1500.0f;
                        if ((p->get_pos() - get_pos()).length_sq() < gun_range * gun_range)
                            hud_mgun = true;
                    }
                    else if (++first_target != targets.end() && p == first_target->target_plane.lock())
                        select = gui::hud::select_next;
                }

                auto fp = std::find_if(targets.begin(), targets.end(), [p](target_lock &t){ return p == t.target_plane.lock(); });
                if (fp == targets.end())
                    continue;

                if (fp->locked > 0)
                    target = gui::hud::target_air_lock;
            }

            h.add_target(p->get_pos(), p->get_rot().get_euler().y, target, select);
        }

        h.set_mgun(hud_mgun);
    }
*/
    last_controls = controls;
    alert_dirs.clear();
}

//------------------------------------------------------------

void plane::update_hud(world &w, gui::hud &h)
{
    h.set_hide(hp <= 0);
    if (hp <= 0)
        return;

    const auto dir = get_dir();
    const auto proj_dir = dir * 1000.0f;
    h.set_project_pos(phys->pos + proj_dir);
    h.set_pos(phys->pos);
    h.set_yaw(phys->rot.get_euler().y);
    h.set_speed(phys->vel.length() * meps_to_kmph);
    h.set_alt(phys->pos.y);
    h.set_jammed(jammed);

    //missile alerts

    h.clear_alerts();
    for(auto &a: alert_dirs)
        h.add_alert(-nya_math::vec2(dir.x, dir.z).angle(nya_math::vec2(a.x, a.z)));

    //update weapon reticle

    if (special.id == "SAAM")
    {
        const bool saam_locked = false; //ToDo

        h.set_saam(saam_locked, saam_locked && !saam_missile.expired());
        h.set_lock(0, saam_locked && special_cooldown[0] <= 0, special_cooldown[0] <= 0);
        h.set_lock(1, saam_locked && special_cooldown[0] > 0, special_cooldown[1] <= 0);
    }
    else if (special.id == "MGP")
        h.set_mgp(special_weapon_selected && controls.missile);

    //update weapon icons

    if (special_weapon_selected != h.is_special_selected())
    {
        if (special_weapon_selected)
        {
            h.set_missiles(special.id.c_str(), 1); //ToDo: idx
            int lock_count = special.lockon_count > 2 ? (int)special.lockon_count : 2;
            if (special.id == "MGP" || special.id == "ECM")
                lock_count = 1;
            h.set_locks(lock_count, 0);
            for (int i = 0; i < lock_count; ++i)
                h.set_lock(i, false, true);
            for (int i = 0; i < (int)special_mount_cooldown.size(); ++i)
                h.set_lock(i, false, special_mount_cooldown[i] < 0);
            h.set_missiles_count(special_count);
            if (special.id == "SAAM")
                h.set_saam_circle(true, acosf(special.lockon_angle_cos));
        }
        else
        {
            h.set_missiles(missile.id.c_str(), 0);
            h.set_locks(0, 0);
            h.set_missiles_count(missile_count);
            h.set_saam_circle(false, 0.0f);
            h.set_mgp(false);
        }
    }

    //update reload icons

    if (special_weapon_selected)
    {
        if (special.lockon_count > 1)
        {
            float value = 1.0f - float(special_cooldown[0]) / special.reload_time;
            for (int i = 0; i < special.lockon_count; ++i)
                h.set_missile_reload(i, value);
        }
        else
        {
            h.set_missile_reload(0, 1.0f - float(special_cooldown[0]) / special.reload_time);
            h.set_missile_reload(1, 1.0f - float(special_cooldown[1]) / special.reload_time);
        }
    }
    else
    {
        h.set_missile_reload(0, 1.0f - float(missile_cooldown[0]) / missile.reload_time);
        h.set_missile_reload(1, 1.0f - float(missile_cooldown[1]) / missile.reload_time);
    }

    //controls

    if (controls.change_radar && controls.change_radar != last_controls.change_radar)
        h.change_radar();

    //should it be there ?

    if (controls.change_camera && controls.change_camera != last_controls.change_camera)
    {
        switch (render->get_camera_mode())
        {
            case renderer::aircraft::camera_mode_third: render->set_camera_mode(renderer::aircraft::camera_mode_cockpit); break;
            case renderer::aircraft::camera_mode_cockpit: render->set_camera_mode(renderer::aircraft::camera_mode_first); break;
            case renderer::aircraft::camera_mode_first: render->set_camera_mode(renderer::aircraft::camera_mode_third); break;
        }
    }
}

//------------------------------------------------------------

void plane::take_damage(int damage, world &w)
{
    if (hp <= 0)
        return;

    object::take_damage(damage, w);
    if (hp <= 0)
    {
        render->set_dead(true);
        w.spawn_explosion(get_pos(), 0, 30.0f);
        if (!saam_missile.expired())
            saam_missile.lock()->target.reset();
    }
}

//------------------------------------------------------------

void missile::update_homing(int dt)
{
    if (target.expired())
        return;

    const vec3 dir = phys->rot.rotate(vec3(0.0, 0.0, 1.0));
    auto t = target.lock();
    auto diff = t->get_pos() - phys->pos;
    const vec3 target_dir = (diff + (t->phys->vel - phys->vel) * dt * 0.001f).normalize();
    if (dir.dot(target_dir) > homing_angle_cos)
    {
        phys->target_dir = target_dir;
        t->alert_dirs.push_back(diff);
    }
}

//------------------------------------------------------------

void missile::update(int dt, world &w)
{
    render->mdl.set_pos(phys->pos);
    render->mdl.set_rot(phys->rot);

    render->engine_started = phys->accel_started;

    if (!target.expired())
    {
        auto dir = target.lock()->get_pos() - phys->pos;
        if (dir.length() < 6.0) //proximity detonation
        {
            int missile_damage = 80; //ToDo
            time = 0;
            //if (vec3::normalize(target.lock()->phys->vel) * dir.normalize() < -0.5)  //direct shoot
            //    missile_damage *= 3;

            auto t = target.lock();
            if (t->hp > 0)
            {
                t->take_damage(missile_damage, w);
                if (t->hp <= 0)
                    w.on_kill(owner.lock(), t);
            }

            w.spawn_explosion(phys->pos, 0, 10.0);
        }

        if (target.lock()->hp < 0)
            target.reset();
    }

    if (time > 0)
        time -= dt;
}

//------------------------------------------------------------
}
