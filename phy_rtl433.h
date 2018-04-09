/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef __PHY_RTL433_H__
#define __PHY_RTL433_H__

#include "config.h"
#include "globalregistry.h"
#include "kis_net_microhttpd.h"
#include "trackedelement.h"
#include "devicetracker_component.h"
#include "phyhandler.h"
#include "kismet_json.h"

class packet_info_rtl433 : public packet_component {
public:
    packet_info_rtl433(Json::Value in_json) {
        json = in_json;
        self_destruct = 1;
    }

    virtual ~packet_info_rtl433() { }

    Json::Value json;
};

/* Similar to the extreme aggregator, a temperature aggregator which ignores empty
 * slots while aggregating and otherwise selects the most extreme value when a 
 * slot overlaps.  This fits a lot of generic situations in RTL433 sensors which
 * only report a few times a second (if that).
 */
class rtl433_empty_aggregator {
public:
    // Select the most extreme value
    static int64_t combine_element(const int64_t a, const int64_t b) {
        if (a < 0 && b < 0) {
            if (a < b)
                return a;

            return b;
        } else if (a > 0 && b > 0) {
            if (a > b)
                return a;

            return b;
        } else if (a == 0) {
            return b;
        } else if (b == 0) {
            return a;
        } else if (a < b) {
            return a;
        }

        return b;
    }

    // Simple average
    static int64_t combine_vector(SharedTrackerElement e) {
        TrackerElementVector v(e);

        int64_t avg = 0;
        int64_t avg_c = 0;

        for (TrackerElementVector::iterator i = v.begin(); i != v.end(); ++i)  {
            int64_t v = GetTrackerValue<int64_t>(*i);

            if (v != default_val()) {
                avg += GetTrackerValue<int64_t>(*i);
                avg_c++;
            }
        }

        if (avg_c == 0)
            return default_val();

        return avg / avg_c;
    }

    // Default 'empty' value, no legit signal would be 0
    static int64_t default_val() {
        return (int64_t) -9999;
    }

    static std::string name() {
        return "rtl433_empty";
    }
};


// Base rtl device record
class rtl433_tracked_common : public tracker_component {
public:
    rtl433_tracked_common(GlobalRegistry *in_globalreg, int in_id) :
        tracker_component(in_globalreg, in_id) {
            register_fields();
            reserve_fields(NULL);
        }

    virtual SharedTrackerElement clone_type() {
        return SharedTrackerElement(new rtl433_tracked_common(globalreg, get_id()));
    }

    rtl433_tracked_common(GlobalRegistry *in_globalreg, int in_id, 
            SharedTrackerElement e) :
        tracker_component(in_globalreg, in_id) {
        register_fields();
        reserve_fields(e);
    }

    __Proxy(model, std::string, std::string, std::string, model);
    __Proxy(rtlid, std::string, std::string, std::string, rtlid);
    __Proxy(rtlchannel, std::string, std::string, std::string, rtlchannel);
    __Proxy(battery, std::string, std::string, std::string, battery);

protected:
    virtual void register_fields() {
        tracker_component::register_fields();

        model_id =
            RegisterField("rtl433.device.model", TrackerString,
                    "Sensor model", &model);

        rtlid_id =
            RegisterField("rtl433.device.id", TrackerString,
                    "Sensor ID", &rtlid);

        rtlchannel_id =
            RegisterField("rtl433.device.rtlchannel", TrackerString,
                    "Sensor sub-channel", &rtlchannel);

        battery_id =
            RegisterField("rtl433.device.battery", TrackerString,
                    "Sensor battery level", &battery);
    }

    int model_id;
    SharedTrackerElement model;

    // Device id, could be from the "id" or the "device" record
    int rtlid_id;
    SharedTrackerElement rtlid;

    // RTL subchannel, if one is available (many thermometers report one)
    int rtlchannel_id;
    SharedTrackerElement rtlchannel;

    // Battery as a string
    int battery_id;
    SharedTrackerElement battery;
};

// Thermometer type rtl data, derived from the rtl device.  This adds new
// fields for thermometers but uses the same base IDs
class rtl433_tracked_thermometer : public tracker_component {
public:
    rtl433_tracked_thermometer(GlobalRegistry *in_globalreg, int in_id) :
       tracker_component(in_globalreg, in_id) {
            register_fields();
            reserve_fields(NULL);
        }

    virtual SharedTrackerElement clone_type() {
        return SharedTrackerElement(new rtl433_tracked_thermometer(globalreg, get_id()));
    }

    rtl433_tracked_thermometer(GlobalRegistry *in_globalreg, int in_id, 
            SharedTrackerElement e) :
        tracker_component(in_globalreg, in_id) {
        register_fields();
        reserve_fields(e);
    }

    __Proxy(temperature, double, double, double, temperature);
    __Proxy(humidity, int32_t, int32_t, int32_t, humidity);

    typedef kis_tracked_rrd<rtl433_empty_aggregator> rrdt;
    __ProxyTrackable(temperature_rrd, rrdt, temperature_rrd);
    __ProxyTrackable(humidity_rrd, rrdt, humidity_rrd);

protected:
    virtual void register_fields() {
        temperature_id =
            RegisterField("rtl433.device.temperature", TrackerDouble,
                    "Temperature in degrees Celsius", &temperature);

        std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > rrd_builder(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        temperature_rrd_id =
            RegisterComplexField("rtl433.device.temperature_rrd", rrd_builder,
                    "Temperature RRD");

        humidity_id =
            RegisterField("rtl433.device.humidity", TrackerInt32,
                    "Humidity", &humidity);

        rrd_builder.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        humidity_rrd_id =
            RegisterComplexField("rtl433.device.humidity_rrd", rrd_builder,
                    "Humidity RRD");
    }

    virtual void reserve_fields(SharedTrackerElement e) {
        tracker_component::reserve_fields(e);

        if (e != NULL) {
            temperature_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        temperature_rrd_id, e->get_map_value(temperature_rrd_id)));
            add_map(temperature_rrd);

            humidity_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        humidity_rrd_id, e->get_map_value(humidity_rrd_id)));
            add_map(humidity_rrd);
        } else {
            temperature_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        temperature_rrd_id));
            add_map(temperature_rrd);

            humidity_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        humidity_rrd_id));
            add_map(humidity_rrd);
        }
    }

    // Basic temp in C, from multiple sensors; we might have to convert to C
    // for some types of sensors
    int temperature_id;
    SharedTrackerElement temperature;

    int temperature_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > temperature_rrd;

    // Basic humidity in percentage, from multiple sensors
    int humidity_id;
    SharedTrackerElement humidity;

    int humidity_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > humidity_rrd;
};

// Weather station type data
class rtl433_tracked_weatherstation : public tracker_component {
public:
    rtl433_tracked_weatherstation(GlobalRegistry *in_globalreg, int in_id) :
        tracker_component(in_globalreg, in_id) {
            register_fields();
            reserve_fields(NULL);
        }

    virtual SharedTrackerElement clone_type() {
        return SharedTrackerElement(new rtl433_tracked_weatherstation(globalreg, get_id()));
    }

    rtl433_tracked_weatherstation(GlobalRegistry *in_globalreg, int in_id, 
            SharedTrackerElement e) :
        tracker_component(in_globalreg, in_id) {
        register_fields();
        reserve_fields(e);
    }

    __Proxy(wind_dir, int32_t, int32_t, int32_t, wind_dir);
    __Proxy(wind_speed, int32_t, int32_t, int32_t, wind_speed);
    __Proxy(wind_gust, int32_t, int32_t, int32_t, wind_gust);
    __Proxy(rain, int32_t, int32_t, int32_t, rain);
    __Proxy(uv_index, int32_t, int32_t, int32_t, uv_index);
    __Proxy(lux, int32_t, int32_t, int32_t, lux);

    typedef kis_tracked_rrd<rtl433_empty_aggregator> rrdt;
    __ProxyTrackable(wind_dir_rrd, rrdt, wind_dir_rrd);
    __ProxyTrackable(wind_speed_rrd, rrdt, wind_speed_rrd);
    __ProxyTrackable(wind_gust_rrd, rrdt, wind_gust_rrd);
    __ProxyTrackable(rain_rrd, rrdt, rain_rrd);
    __ProxyTrackable(uv_index_rrd, rrdt, uv_index_rrd);
    __ProxyTrackable(lux_rrd, rrdt, lux_rrd);

protected:
    virtual void register_fields() {
        RegisterField("rtl433.device.wind_dir", TrackerInt32,
                "Wind direction in degrees", &wind_dir);

        std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > rrd_builder(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        wind_dir_rrd_id =
            RegisterComplexField("rtl433.device.wind_dir_rrd", rrd_builder,
                    "Wind direction RRD");

        RegisterField("rtl433.device.weatherstation.wind_speed", TrackerInt32,
                "Wind speed in Kph", &wind_speed);

        rrd_builder.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        wind_speed_rrd_id =
            RegisterComplexField("rtl433.device.wind_speed_rrd", rrd_builder,
                    "Wind speed RRD");

        RegisterField("rtl433.device.wind_gust", TrackerInt32,
                "Wind gust in Kph", &wind_gust);

        rrd_builder.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        wind_gust_rrd_id =
            RegisterComplexField("rtl433.device.wind_gust_rrd", rrd_builder,
                    "Wind gust RRD");

        RegisterField("rtl433.device.rain", TrackerInt32,
                "Measured rain", &rain);

        rrd_builder.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        rain_rrd_id =
            RegisterComplexField("rtl433.device.rain_rrd", rrd_builder,
                    "Rain RRD");

        RegisterField("rtl433.device.uv_index", TrackerInt32,
                "UV index", &uv_index);
        rrd_builder.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        uv_index_rrd_id =
            RegisterComplexField("rtl433.device.uv_index_rrd", rrd_builder,
                    "UV index RRD");

        RegisterField("rtl433.device.lux", TrackerInt32,
                "Lux", &lux);
        rrd_builder.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 0));
        lux_rrd_id =
            RegisterComplexField("rtl433.device.lux_rrd", rrd_builder,
                    "Lux RRD");
    }

    virtual void reserve_fields(SharedTrackerElement e) {
        tracker_component::reserve_fields(e);

        if (e != NULL) {
            wind_dir_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        wind_dir_rrd_id, e->get_map_value(wind_dir_rrd_id)));
            add_map(wind_dir_rrd);

            wind_speed_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        wind_speed_rrd_id, e->get_map_value(wind_speed_rrd_id)));
            add_map(wind_speed_rrd);

            wind_gust_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        wind_gust_rrd_id, e->get_map_value(wind_gust_rrd_id)));
            add_map(wind_gust_rrd);

            rain_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        rain_rrd_id, e->get_map_value(rain_rrd_id)));
            add_map(rain_rrd);

            uv_index_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        uv_index_rrd_id, e->get_map_value(uv_index_rrd_id)));
            add_map(uv_index_rrd);

            lux_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, lux_rrd_id,
                        e->get_map_value(lux_rrd_id)));
            add_map(lux_rrd);
        } else {
            wind_dir_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        wind_dir_rrd_id));
            add_map(wind_dir_rrd);

            wind_speed_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        wind_speed_rrd_id));
            add_map(wind_speed_rrd);

            wind_gust_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        wind_gust_rrd_id));
            add_map(wind_gust_rrd);

            rain_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, rain_rrd_id));
            add_map(rain_rrd);

            uv_index_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, 
                        uv_index_rrd_id));
            add_map(uv_index_rrd);

            lux_rrd.reset(new kis_tracked_rrd<rtl433_empty_aggregator>(globalreg, lux_rrd_id));
            add_map(lux_rrd);
        }
    }

    // Wind direction in degrees
    SharedTrackerElement wind_dir;

    int wind_dir_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > wind_dir_rrd;

    // Wind speed in kph (might have to convert for some sensors)
    SharedTrackerElement wind_speed;

    int wind_speed_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > wind_speed_rrd;

    // Wind gust in kph (might have to convert for some sensors)
    SharedTrackerElement wind_gust;

    int wind_gust_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > wind_gust_rrd;

    // Rain (in whatever the sensor reports it in)
    SharedTrackerElement rain;

    int rain_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > rain_rrd;

    // UV
    SharedTrackerElement uv_index;

    int uv_index_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > uv_index_rrd;

    // Lux
    SharedTrackerElement lux;

    int lux_rrd_id;
    std::shared_ptr<kis_tracked_rrd<rtl433_empty_aggregator> > lux_rrd;
};

// TPMS tire pressure sensors
class rtl433_tracked_tpms : public tracker_component {
public:
    rtl433_tracked_tpms(GlobalRegistry *in_globalreg, int in_id) :
       tracker_component(in_globalreg, in_id) {
            register_fields();
            reserve_fields(NULL);
        }

    virtual SharedTrackerElement clone_type() {
        return SharedTrackerElement(new rtl433_tracked_tpms(globalreg, get_id()));
    }

    rtl433_tracked_tpms(GlobalRegistry *in_globalreg, int in_id, 
            SharedTrackerElement e) :
        tracker_component(in_globalreg, in_id) {
        register_fields();
        reserve_fields(e);
    }

    __Proxy(pressure_bar, double, double, double, pressure_bar);
    __Proxy(flags, std::string, std::string, std::string, flags);
    __Proxy(state, std::string, std::string, std::string, state);
    __Proxy(checksum, std::string, std::string, std::string, checksum);
    __Proxy(code, std::string, std::string, std::string, code);

protected:
    virtual void register_fields() {
        RegisterField("rtl433.device.tpms.pressure_bar", TrackerDouble,
                "Pressure, in bars", &pressure_bar);
        RegisterField("rtl433.device.tpms.flags", TrackerString,
                "TPMS flags", &flags);
        RegisterField("rtl433.device.tpms.state", TrackerString,
                "TPMS state", &state);
        RegisterField("rtl433.device.tpms.checksum", TrackerString,
                "TPMS checksum", &checksum);
        RegisterField("rtl433.device.tpms.code", TrackerString,
                "TPMS code", &code);
    }

    virtual void reserve_fields(SharedTrackerElement e) {
        tracker_component::reserve_fields(e);
    }

    SharedTrackerElement pressure_bar;
    SharedTrackerElement checksum;
    SharedTrackerElement flags;
    SharedTrackerElement state;
    SharedTrackerElement code;
};

// Switch panels
class rtl433_tracked_switch : public tracker_component {
public:
    rtl433_tracked_switch(GlobalRegistry *in_globalreg, int in_id) :
       tracker_component(in_globalreg, in_id) {
            register_fields();
            reserve_fields(NULL);
        }

    virtual SharedTrackerElement clone_type() {
        return SharedTrackerElement(new rtl433_tracked_switch(globalreg, get_id()));
    }

    rtl433_tracked_switch(GlobalRegistry *in_globalreg, int in_id, 
            SharedTrackerElement e) :
        tracker_component(in_globalreg, in_id) {
        register_fields();
        reserve_fields(e);
    }

    __ProxyTrackable(switch_vec, TrackerElement, switch_vec);

protected:
    virtual void register_fields() {
        RegisterField("rtl433.device.switch_vec", TrackerVector,
                "Switch settings", &switch_vec);
        switch_vec_entry_id = 
            RegisterField("rtl433.device.switch.position", TrackerInt32,
                "Switch position");
    }

    virtual void reserve_fields(SharedTrackerElement e) {
        tracker_component::reserve_fields(e);
    }

    SharedTrackerElement switch_vec;
    int switch_vec_entry_id;

};

class Kis_RTL433_Phy : public Kis_Phy_Handler {
public:
    virtual ~Kis_RTL433_Phy();

    Kis_RTL433_Phy(GlobalRegistry *in_globalreg) :
        Kis_Phy_Handler(in_globalreg) { };

	// Build a strong version of ourselves
	virtual Kis_Phy_Handler *CreatePhyHandler(GlobalRegistry *in_globalreg,
											  Devicetracker *in_tracker,
											  int in_phyid) {
		return new Kis_RTL433_Phy(in_globalreg, in_tracker, in_phyid);
	}

    Kis_RTL433_Phy(GlobalRegistry *in_globalreg, Devicetracker *in_tracker,
            int in_phyid);

    static int PacketHandler(CHAINCALL_PARMS);

protected:
    // Convert a JSON record to a RTL-based device key
    mac_addr json_to_mac(Json::Value in_json);

    // convert to a device record & push into device tracker, return false
    // if we can't do anything with it
    bool json_to_rtl(Json::Value in_json);

    bool is_weather_station(Json::Value json);
    bool is_thermometer(Json::Value json);
    bool is_tpms(Json::Value json);
    bool is_switch(Json::Value json);

    void add_weather_station(Json::Value json, SharedTrackerElement rtlholder);
    void add_thermometer(Json::Value json, SharedTrackerElement rtlholder);
    void add_tpms(Json::Value json, SharedTrackerElement rtlholder);
    void add_switch(Json::Value json, SharedTrackerElement rtlholder);

    double f_to_c(double f);


protected:
    std::shared_ptr<Packetchain> packetchain;
    std::shared_ptr<EntryTracker> entrytracker;

    int rtl433_holder_id, rtl433_common_id, rtl433_thermometer_id, 
        rtl433_weatherstation_id, rtl433_tpms_id, rtl433_switch_id;

    int pack_comp_common, pack_comp_rtl433;

};

#endif

