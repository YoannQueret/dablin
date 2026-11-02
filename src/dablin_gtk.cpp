/*
    DABlin - capital DAB experience
    Copyright (C) 2015-2024 Stefan Pöschel

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dablin_gtk.h"

#include "ecc_countries.h"
#include "edi_network_source.h"

#include <algorithm>
#include <fstream>
#include <sys/stat.h>

static DABlinGTK *dablin = nullptr;

// --- network connect history (persisted to $XDG_CONFIG_HOME/dablin/network_history, or
// ~/.config/dablin/network_history) -----------------------------------------------------------
static const size_t NETWORK_HISTORY_MAX_ENTRIES = 10;

static std::string GetConfigDir() {
	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	std::string base = (xdg_config_home && *xdg_config_home) ? xdg_config_home :
			std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config";
	return base + "/dablin";
}

static std::string GetNetworkHistoryPath() {
	return GetConfigDir() + "/network_history";
}

static std::vector<std::string> LoadNetworkHistory() {
	std::vector<std::string> result;
	std::ifstream file(GetNetworkHistoryPath());
	std::string line;
	while(std::getline(file, line) && result.size() < NETWORK_HISTORY_MAX_ENTRIES)
		if(!line.empty())
			result.push_back(line);
	return result;
}

static void SaveNetworkHistory(const std::vector<std::string>& history) {
	std::string config_dir = GetConfigDir();

	// best-effort directory creation (also covers a missing ~/.config); errors (incl. EEXIST) are ignored
	size_t pos = config_dir.find_last_of('/');
	if(pos != std::string::npos)
		mkdir(config_dir.substr(0, pos).c_str(), 0755);
	mkdir(config_dir.c_str(), 0755);

	std::ofstream file(GetNetworkHistoryPath());
	for(const std::string& uri : history)
		file << uri << "\n";
}

static void break_handler(int) {
	fprintf(stderr, "...DABlin exits...\n");
	if(dablin)
		dablin->hide();
}


// --- DAB live source auto-detection (so SDR reception works out of the box, without
// having to pass -d/-D on the command line) -----------------------------------------------------
static bool FindInPath(const std::string& name, std::string& out_path) {
	const char *path_env = getenv("PATH");
	if(!path_env)
		return false;

	std::string path(path_env);
	size_t start = 0;
	while(start <= path.size()) {
		size_t end = path.find(':', start);
		if(end == std::string::npos)
			end = path.size();

		std::string dir = path.substr(start, end - start);
		if(!dir.empty()) {
			std::string candidate = dir + "/" + name;
			if(access(candidate.c_str(), X_OK) == 0) {
				out_path = candidate;
				return true;
			}
		}
		start = end + 1;
	}
	return false;
}

static std::string GetExecutableDir() {
	char buf[4096];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if(len <= 0)
		return "";
	buf[len] = '\0';

	std::string path(buf);
	size_t pos = path.find_last_of('/');
	return pos == std::string::npos ? "" : path.substr(0, pos);
}

// tries the known DAB live source tools, in order of preference
static bool DetectLiveSourceBinary(std::string& binary, std::string& type) {
	// prefer dablin's own bundled eti-cmdline (built alongside this executable), if present
	std::string exe_dir = GetExecutableDir();
	if(!exe_dir.empty()) {
		std::string candidate = exe_dir + "/eti-cmdline-rtlsdr";
		if(access(candidate.c_str(), X_OK) == 0) {
			binary = candidate;
			type = DABLiveETISource::TYPE_ETI_CMDLINE;
			return true;
		}
	}

	if(FindInPath("eti-cmdline", binary)) {
		type = DABLiveETISource::TYPE_ETI_CMDLINE;
		return true;
	}
	if(FindInPath("dab2eti", binary)) {
		type = DABLiveETISource::TYPE_DAB2ETI;
		return true;
	}
	return false;
}


static void usage(const char* exe) {
	DABlinGTKOptions options_default;

	fprint_dablin_banner(stderr);
	fprintf(stderr, "Usage: %s [OPTIONS] [file]\n", exe);
	fprintf(stderr, "  -h           Show this help\n"
					"  -f <format>  Source format: \"%s\" (default), \"%s\"\n"
					"  -d <binary>  Use DAB live source (using the mentioned binary)\n"
					"  -D <type>    DAB live source type: \"%s\" (default), \"%s\"\n"
					"  -C <ch>,...  Channels to be listed (comma separated; requires DAB live source;\n"
					"               an optional gain can also be specified, e.g. \"5C:-54\")\n"
					"  -c <ch>      Channel to be played (requires DAB live source)\n"
					"  -l <label>   Label of the service to be played\n"
					"  -1           Play the first service found\n"
					"  -s <sid>     ID of the service to be played\n"
					"  -x <scids>   ID of the service component to be played (requires service ID)\n"
					"  -g <gain>    USB stick gain to pass to DAB live source (auto gain is default)\n"
					"  -G           Use default gain for DAB live source (instead of auto gain)\n"
					"  -r <path>    Path for recordings (default: %s)\n"
					"  -P <size>    Recording prebuffer size in seconds (default: %zu)\n"
					"  -p           Output raw PCM to stdout instead of using SDL\n"
					"  -w           Output RIFF WAVE with PCM to stdout instead of using SDL (useful only on Little Endian)\n"
					"  -u           Output untouched audio stream to stdout instead of using SDL\n"
					"  -I           Don't catch up on stream after interruption\n"
					"  -L           Enable loose behaviour (e.g. PAD conformance)\n"
					"  -F           Disable dynamic FIC messages (dynamic PTY, announcements)\n"
					"  file         Input file to be played (stdin, if not specified)\n",
					EnsembleSource::FORMAT_ETI.c_str(),
					EnsembleSource::FORMAT_EDI.c_str(),
					DABLiveETISource::TYPE_DAB2ETI.c_str(),
					DABLiveETISource::TYPE_ETI_CMDLINE.c_str(),
					options_default.recordings_path.c_str(),
					options_default.rec_prebuffer_size_s
			);
	exit(1);
}


int main(int argc, char **argv) {
	// handle signals
	if(signal(SIGINT, break_handler) == SIG_ERR) {
		perror("DABlin: error while setting SIGINT handler");
		return 1;
	}
	if(signal(SIGTERM, break_handler) == SIG_ERR) {
		perror("DABlin: error while setting SIGTERM handler");
		return 1;
	}

	DABlinGTKOptions options;
	int initial_param_count = 0;
	int gain_param_count = 0;

	// option args
	int c;
	while((c = getopt(argc, argv, "hf:d:D:C:c:l:g:Gr:P:s:x:1pwuILF")) != -1) {
		switch(c) {
		case 'h':
			usage(argv[0]);
			break;
		case 'f':
			options.source_format = optarg;
			break;
		case 'd':
			options.dab_live_source_binary = optarg;
			break;
		case 'D':
			options.dab_live_source_type = optarg;
			break;
		case 'C':
			options.displayed_channels = optarg;
			break;
		case 'c':
			options.initial_channel = optarg;
			break;
		case 'l':
			options.initial_label = optarg;
			initial_param_count++;
			break;
		case '1':
			options.initial_first_found_service = true;
			initial_param_count++;
			break;
		case 's':
			options.initial_sid = strtol(optarg, nullptr, 0);
			initial_param_count++;
			break;
		case 'x':
			options.initial_scids = strtol(optarg, nullptr, 0);
			break;
		case 'g':
			options.gain = strtol(optarg, nullptr, 0);
			gain_param_count++;
			break;
		case 'G':
			options.gain = DAB_LIVE_SOURCE_CHANNEL::default_gain;
			gain_param_count++;
			break;
		case 'r':
			options.recordings_path = optarg;
			break;
		case 'P':
			options.rec_prebuffer_size_s = strtol(optarg, nullptr, 0);
			break;
		case 'p':
			options.pcm_output = true;
			break;
		case 'w':
			options.wav_output = true;
			break;
		case 'u':
			options.untouched_output = true;
			break;
		case 'I':
			options.disable_int_catch_up = true;
			break;
		case 'L':
			options.loose = true;
			break;
		case 'F':
			options.disable_dyn_fic_msgs = true;
			break;
		case '?':
		default:
			usage(argv[0]);
		}
	}

	// non-option args
	switch(argc - optind) {
	case 0:
		break;
	case 1:
		options.filename = argv[optind];
		break;
	default:
		usage(argv[0]);
	}

	// auto-detect a DAB live source (RTL-SDR via eti-cmdline/dab2eti) when nothing else
	// was explicitly requested, so channel selection works out of the box without -d/-D
	if(options.dab_live_source_binary.empty() && options.filename.empty() && options.source_format == EnsembleSource::FORMAT_ETI) {
		std::string detected_binary, detected_type;
		if(DetectLiveSourceBinary(detected_binary, detected_type)) {
			fprintf(stderr, "DABlinGTK: auto-detected DAB live source '%s'\n", detected_binary.c_str());
			options.dab_live_source_binary = detected_binary;
			options.dab_live_source_type = detected_type;
		}
	}

	// ensure valid options
	if(options.dab_live_source_binary.empty()) {
		if(!options.displayed_channels.empty()) {
			fprintf(stderr, "If displayed channels are selected, DAB live source must be used!\n");
			usage(argv[0]);
		}
		if(!options.initial_channel.empty()) {
			fprintf(stderr, "If a channel is selected, DAB live source must be used!\n");
			usage(argv[0]);
		}
	} else {
		if(options.source_format != EnsembleSource::FORMAT_ETI) {
			fprintf(stderr, "A DAB live source can only be used with ETI source format!\n");
			usage(argv[0]);
		}
		if(!options.filename.empty()) {
			fprintf(stderr, "Both a file and DAB live source cannot be used as source!\n");
			usage(argv[0]);
		}
		if(!options.initial_channel.empty() && dab_channels.find(options.initial_channel) == dab_channels.end()) {
			fprintf(stderr, "The channel '%s' is not supported!\n", options.initial_channel.c_str());
			usage(argv[0]);
		}
		if(options.dab_live_source_type != DABLiveETISource::TYPE_DAB2ETI && options.dab_live_source_type != DABLiveETISource::TYPE_ETI_CMDLINE) {
			fprintf(stderr, "The DAB live source type '%s' is not supported!\n", options.dab_live_source_type.c_str());
			usage(argv[0]);
		}
	}
	if(options.initial_scids != LISTED_SERVICE::scids_none && options.initial_sid == LISTED_SERVICE::sid_none) {
		fprintf(stderr, "The service component ID requires the service ID to be specified!\n");
		usage(argv[0]);
	}
#ifdef DABLIN_DISABLE_SDL
	if(!options.pcm_output) {
		fprintf(stderr, "SDL output was disabled, so PCM output must be selected!\n");
		usage(argv[0]);
	}
#endif

	if(options.pcm_output + options.wav_output + options.untouched_output > 1) {
		fprintf(stderr, "No more than one output option can be specified!\n");
		usage(argv[0]);
	}


	// at most one param needed!
	if(initial_param_count > 1) {
		fprintf(stderr, "At most one initial parameter shall be specified!\n");
		usage(argv[0]);
	}
	if(gain_param_count > 1) {
		fprintf(stderr, "At most one gain parameter shall be specified!\n");
		usage(argv[0]);
	}


	fprint_dablin_banner(stderr);

	int myargc = 1;
	Glib::RefPtr<Gtk::Application> app = Gtk::Application::create(myargc, argv, "");

	dablin = new DABlinGTK(options);
	int result = app->run(*dablin);
	delete dablin;

	return result;
}



// --- DABlinGTK -----------------------------------------------------------------
DABlinGTK::DABlinGTK(DABlinGTKOptions options) : combo_network_uri(true) {
	this->options = options;

	switch_service_label = options.initial_label;
	switch_service_sid = options.initial_sid;
	switch_service_scids = options.initial_scids;
	switch_service_applied = false;

	rec_file = nullptr;
	rec_duration_ms = 0;
	rec_prebuffer_filled_ms = 0;

	dt_lto = FIC_ENSEMBLE::lto_none;

	current_audio_format_subchid = AUDIO_SERVICE::subchid_none;

	logs_window.set_transient_for(*this);

	ensemble_update_progress.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::EnsembleUpdateProgressEmitted));
	ensemble_change_format.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::EnsembleChangeFormatEmitted));
	ensemble_audio_level.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::EnsembleAudioLevelEmitted));
	ensemble_audio_spectrum.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::EnsembleAudioSpectrumEmitted));
	fic_change_ensemble.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICChangeEnsembleEmitted));
	fic_change_service.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICChangeServiceEmitted));
	fic_remove_service.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICRemoveServiceEmitted));
	fic_change_subchannel.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICChangeSubchannelEmitted));
	fic_remove_subchannel.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICRemoveSubchannelEmitted));
	fic_change_data_service_sid.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICChangeDataServiceSIDEmitted));
	fic_remove_data_service_sid.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICRemoveDataServiceSIDEmitted));
	fic_log_message.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::FICLogMessageEmitted));
	pad_change_dynamic_label.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::PADChangeDynamicLabelEmitted));
	pad_change_slide.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::PADChangeSlideEmitted));
	pad_file_progress.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::PADFileProgressEmitted));
	do_rec_status_update.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::DoRecStatusUpdateEmitted));
	do_datetime_sync.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::DoDateTimeSyncEmitted));
	do_datetime_update.GetDispatcher().connect(sigc::mem_fun(*this, &DABlinGTK::DoDateTimeUpdateEmitted));

	audio_output_type = AudioOutputType::SDL;
#ifndef DABLIN_DISABLE_SDL
	if(options.pcm_output)
#endif
		audio_output_type = AudioOutputType::PCM;
	if(options.wav_output)
		audio_output_type = AudioOutputType::WAV;
	if(options.untouched_output)
		audio_output_type = AudioOutputType::Untouched;

	if(options.source_format == EnsembleSource::FORMAT_ETI)
		ensemble_player = new ETIPlayer(audio_output_type, options.disable_int_catch_up, this);
	else
		ensemble_player = new EDIPlayer(audio_output_type, options.disable_int_catch_up, this);

	if(options.source_format == EnsembleSource::FORMAT_ETI) {
		if(!options.dab_live_source_binary.empty()) {
			ensemble_source = nullptr;
		} else {
			ensemble_source = new ETISource(options.filename, this);
			ensemble_source_thread = std::thread(&EnsembleSource::Main, ensemble_source);
		}
	} else {
		ensemble_source = new EDISource(options.filename, this);
		ensemble_source_thread = std::thread(&EnsembleSource::Main, ensemble_source);
	}

	fic_decoder = new FICDecoder(this, options.disable_dyn_fic_msgs);
	pad_decoder = new PADDecoder(this, options.loose);

	set_title("DABlin v" + std::string(DABLIN_VERSION));
	set_icon_name("media-playback-stop");

	InitWidgets();

	set_border_width(2 * WIDGET_SPACE);

	// combobox first must be visible
	if(combo_channels_liststore->iter_is_valid(initial_channel_it))
		combo_channels.set_active(initial_channel_it);

	// add window key press event handler
	signal_key_press_event().connect(sigc::mem_fun(*this, &DABlinGTK::HandleKeyPressEvent));
	add_events(Gdk::KEY_PRESS_MASK);

	// add window config event handler (before default handler)
	signal_configure_event().connect(sigc::mem_fun(*this, &DABlinGTK::HandleConfigureEvent), false);
	add_events(Gdk::STRUCTURE_MASK);
}

DABlinGTK::~DABlinGTK() {
	if(ensemble_source) {
		ensemble_source->DoExit();
		ensemble_source_thread.join();
		delete ensemble_source;
	}

	delete ensemble_player;

	delete pad_decoder;
	delete fic_decoder;
}

int DABlinGTK::ComboChannelsSlotCompare(const Gtk::TreeModel::iterator& a, const Gtk::TreeModel::iterator& b) {
	const DAB_LIVE_SOURCE_CHANNEL& ch_a = (DAB_LIVE_SOURCE_CHANNEL) (*a)[combo_channels_cols.col_channel];
	const DAB_LIVE_SOURCE_CHANNEL& ch_b = (DAB_LIVE_SOURCE_CHANNEL) (*b)[combo_channels_cols.col_channel];

	if(ch_a < ch_b)
		return -1;
	if(ch_b < ch_a)
		return 1;
	return 0;
}

int DABlinGTK::ServicesTableSlotCompare(const Gtk::TreeModel::iterator& a, const Gtk::TreeModel::iterator& b) {
	const LISTED_SERVICE& service_a = (LISTED_SERVICE) (*a)[services_table_cols.col_service];
	const LISTED_SERVICE& service_b = (LISTED_SERVICE) (*b)[services_table_cols.col_service];

	if(service_a < service_b)
		return -1;
	if(service_b < service_a)
		return 1;
	return 0;
}

void DABlinGTK::RenderServiceDetailCell(Gtk::CellRenderer* renderer, const Gtk::TreeModel::iterator& it, int column_id) {
	Gtk::CellRendererText *text_renderer = static_cast<Gtk::CellRendererText*>(renderer);
	const LISTED_SERVICE& service = (LISTED_SERVICE) (*it)[services_table_cols.col_service];

	std::string text;
	switch(column_id) {
	case 0:	// SubChId
		text = std::to_string(service.subchid);
		break;
	case 1:	// SID (without the "0x" prefix, to save space in the table) - unknown for a plain data sub-channel
		if(service.sid != LISTED_SERVICE::sid_none)
			text = StringTools::SidToHex(service.sid);
		break;
	case 2:	// ECC (without the "0x" prefix), only shown if it differs from the ensemble's own (FIG 0/9 exception)
		if(service.ecc != LISTED_SERVICE::ecc_none)
			text = StringTools::IntToHex(service.ecc, 2).substr(2);
		break;
	case 3:	{ // Label
		if(!service.is_audio) {
			text = "(data service)";
			break;
		}
		text = FICDecoder::ConvertLabelToUTF8(service.label, nullptr);
		if(service.multi_comps)
			text = (!service.IsPrimary() ? "» " : "") + text + (service.IsPrimary() ? " »" : "");
		break; }
	case 4:	{ // Short label
		std::string label = FICDecoder::ConvertLabelToUTF8(service.label, nullptr);
		text = FICDecoder::DeriveShortLabelUTF8(label, service.label.short_label_mask);
		break; }
	case 5:	// CUs
		if(!service.subchannel.IsNone() && !service.subchannel.pl.empty())
			text = std::to_string(service.subchannel.start) + "-" + std::to_string(service.subchannel.start + service.subchannel.size) +
					" (" + std::to_string(service.subchannel.size) + ")";
		break;
	case 6:	// Protection
		text = service.subchannel.pl;
		break;
	case 7:	// Bit rate
		if(!service.subchannel.IsNone() && !service.subchannel.pl.empty())
			text = std::to_string(service.subchannel.bitrate) + " kBit/s";
		break;
	case 8:	// Format (only known for the currently playing sub-channel, while it is being decoded)
		if(service.is_audio && service.audio_service.subchid == current_audio_format_subchid)
			text = current_audio_format.codec;
		break;
	case 9:	// Samplerate (ditto)
		if(service.is_audio && service.audio_service.subchid == current_audio_format_subchid && current_audio_format.samplerate_khz)
			text = std::to_string(current_audio_format.samplerate_khz) + " kHz";
		break;
	case 10:	// Audio mode (ditto)
		if(service.is_audio && service.audio_service.subchid == current_audio_format_subchid)
			text = current_audio_format.mode;
		break;
	case 11:	// Language
		if(service.subchannel.language != FIC_SUBCHANNEL::language_none)
			text = FICDecoder::ConvertLanguageToString(service.subchannel.language);
		break;
	case 12:	{ // PTY
		if(ensemble.inter_table_id != FIC_ENSEMBLE::inter_table_id_none) {
			int pty = service.pty_dynamic != LISTED_SERVICE::pty_none ? service.pty_dynamic : service.pty_static;
			if(pty != LISTED_SERVICE::pty_none)
				text = FICDecoder::ConvertPTYToString(pty, ensemble.inter_table_id);
		}
		break; }
	}

	text_renderer->property_text() = text;

	// not a playable service - de-emphasize the whole row instead of just leaving cells blank
	text_renderer->property_style() = service.is_audio ? Pango::STYLE_NORMAL : Pango::STYLE_ITALIC;
	text_renderer->property_foreground_set() = !service.is_audio;
	if(!service.is_audio)
		text_renderer->property_foreground() = "grey";
}

bool DABlinGTK::on_services_table_query_tooltip(int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip>& tooltip) {
	if(keyboard_tooltip)
		return false;

	// query-tooltip reports widget coordinates (including the header row), while
	// get_path_at_pos expects bin window coordinates - they must be converted first
	int bin_x, bin_y;
	services_table.convert_widget_to_bin_window_coords(x, y, bin_x, bin_y);

	Gtk::TreeModel::Path path;
	Gtk::TreeViewColumn *column;
	int cell_x, cell_y;
	if(!services_table.get_path_at_pos(bin_x, bin_y, path, column, cell_x, cell_y))
		return false;

	Gtk::TreeModel::iterator it = services_table_liststore->get_iter(path);
	if(!it)
		return false;
	const LISTED_SERVICE& service = (LISTED_SERVICE) (*it)[services_table_cols.col_service];

	// row-wide details - more can be added here later
	if(service.sid == LISTED_SERVICE::sid_none)
		return false;
	std::string text = "SID: " + StringTools::SidToHex(service.sid);

	int ecc = service.ecc != LISTED_SERVICE::ecc_none ? service.ecc : ensemble.ecc;
	if(ecc != FIC_ENSEMBLE::ecc_none) {
		text += "\n" "ECC: " + StringTools::IntToHex(ecc, 2).substr(2);
		// the ECC+country-nibble convention only applies to 16-bit (programme service) SIds
		if(service.sid <= 0xFFFF) {
			const char *country = LookupECCCountry(ecc, service.sid >> 12);
			if(country)
				text += " (" + std::string(country) + ")";
		}
	}

	tooltip->set_text(text);
	services_table.set_tooltip_row(tooltip, path);
	return true;
}

Gtk::ListStore::iterator DABlinGTK::UpdateServicesTableRow(const LISTED_SERVICE& service, bool* was_new) {
	Gtk::ListStore::Children children = services_table_liststore->children();
	Gtk::ListStore::iterator row_it = std::find_if(
			children.begin(), children.end(),
			[&](const Gtk::TreeModel::Row& row)->bool {
				const LISTED_SERVICE& ls = (LISTED_SERVICE) row[services_table_cols.col_service];
				return ls.sid == service.sid && ls.scids == service.scids;
			}
	);
	bool add_new_row = row_it == children.end();
	if(add_new_row)
		row_it = services_table_liststore->append();

	Gtk::TreeModel::Row row = *row_it;
	row[services_table_cols.col_service] = service;

	if(was_new)
		*was_new = add_new_row;
	return row_it;
}

void DABlinGTK::RebuildDataSubchannelRows() {
	// sub-channels already represented by a playable (audio) row
	std::set<int> claimed_subchids;
	for(const Gtk::TreeModel::Row& row : services_table_liststore->children()) {
		const LISTED_SERVICE& ls = (LISTED_SERVICE) row[services_table_cols.col_service];
		if(ls.is_audio)
			claimed_subchids.insert(ls.subchid);
	}

	// drop stale data-only rows (sub-channel gone, or now claimed by an audio service)
	Gtk::ListStore::Children children = services_table_liststore->children();
	for(Gtk::ListStore::iterator it = children.begin(); it != children.end();) {
		const LISTED_SERVICE& ls = (LISTED_SERVICE) (*it)[services_table_cols.col_service];
		if(!ls.is_audio && (known_subchannels.find(ls.subchid) == known_subchannels.end() || claimed_subchids.find(ls.subchid) != claimed_subchids.end()))
			it = services_table_liststore->erase(it);
		else
			++it;
	}

	// add/update a non-playable placeholder row for every remaining (data-only) sub-channel,
	// so it's listed - and its CUs accounted for - even though dablin can't play it
	for(const std::pair<const int, FIC_SUBCHANNEL>& sc : known_subchannels) {
		if(claimed_subchids.find(sc.first) != claimed_subchids.end() || sc.second.IsNone() || sc.second.pl.empty())
			continue;

		children = services_table_liststore->children();
		Gtk::ListStore::iterator row_it = std::find_if(
				children.begin(), children.end(),
				[&](const Gtk::TreeModel::Row& row)->bool {
					const LISTED_SERVICE& ls = (LISTED_SERVICE) row[services_table_cols.col_service];
					return !ls.is_audio && ls.subchid == sc.first;
				}
		);
		if(row_it == children.end())
			row_it = services_table_liststore->append();

		LISTED_SERVICE ls;
		ls.is_audio = false;
		ls.subchid = sc.first;
		ls.pri_comp_subchid = sc.first;
		ls.subchannel = sc.second;
		std::map<int,uint32_t>::const_iterator sid_it = known_data_service_sids.find(sc.first);
		if(sid_it != known_data_service_sids.end())
			ls.sid = (int) sid_it->second;

		Gtk::TreeModel::Row row = *row_it;
		row[services_table_cols.col_service] = ls;
	}
}

void DABlinGTK::InitWidgets() {
	this->signal_delete_event().connect(sigc::mem_fun(*this, &DABlinGTK::on_window_delete_event));

	// init widgets

	// "Source" groups the two mutually exclusive ways to feed the ensemble - DAB live
	// (RTL-SDR) and Network (EDI) - behind one pair of radio-style buttons, so only one
	// of their controls is ever shown/usable at a time (picking one stops the other).
	frame_source.set_label("Source");
	frame_source.add(source_box);
	source_box.set_spacing(WIDGET_SPACE);
	source_box.pack_start(tglbtn_source_dab, Gtk::PACK_SHRINK);
	source_box.pack_start(tglbtn_source_network, Gtk::PACK_SHRINK);
	source_box.pack_start(source_stack, Gtk::PACK_SHRINK);

	tglbtn_source_dab.set_label("DAB");
	tglbtn_source_dab.set_mode(false);	// render as a flat toggle button, not a radio dot
	tglbtn_source_network.set_label("Network");
	tglbtn_source_network.set_mode(false);
	tglbtn_source_network.join_group(tglbtn_source_dab);

	tglbtn_source_dab.signal_toggled().connect([this]() {
		if(tglbtn_source_dab.get_active())
			on_source_type_changed(true);
	});
	tglbtn_source_network.signal_toggled().connect([this]() {
		if(tglbtn_source_network.get_active())
			on_source_type_changed(false);
	});

	source_stack.add(channels_box, "dab");
	source_stack.add(network_box, "network");

	channels_box.pack_start(combo_channels);
	channels_box.pack_start(btn_channels_stop, Gtk::PACK_SHRINK);

	combo_channels_liststore = Gtk::ListStore::create(combo_channels_cols);
	combo_channels_liststore->set_default_sort_func(sigc::mem_fun(*this, &DABlinGTK::ComboChannelsSlotCompare));
	combo_channels_liststore->set_sort_column(Gtk::TreeSortable::DEFAULT_SORT_COLUMN_ID, Gtk::SORT_ASCENDING);

	combo_channels.signal_changed().connect(sigc::mem_fun(*this, &DABlinGTK::on_combo_channels));
	combo_channels.set_model(combo_channels_liststore);
	combo_channels.pack_start(combo_channels_cols.col_string);

	bool dab_available = !options.dab_live_source_binary.empty();
	if(dab_available)
		AddChannels();
	channels_box.set_sensitive(dab_available);
	tglbtn_source_dab.set_sensitive(dab_available);

	btn_channels_stop.set_image_from_icon_name("media-playback-start");
	btn_channels_stop.signal_clicked().connect(sigc::mem_fun(*this, &DABlinGTK::on_btn_channels_stop));
	btn_channels_stop.set_sensitive(false);
	btn_channels_stop.set_tooltip_text("Stop/resume decoding current channel/service");

	network_box.set_spacing(WIDGET_SPACE);
	network_box.pack_start(combo_network_uri);
	network_box.pack_start(tglbtn_network_connect, Gtk::PACK_SHRINK);

	combo_network_uri.get_entry()->set_width_chars(22);
	combo_network_uri.get_entry()->set_placeholder_text("tcp://host:port");
	combo_network_uri.set_tooltip_text(
			"EDI network source\n"
			"tcp://host:port\n"
			"udp://host:port (unicast)\n"
			"udp://mcast-ip:port (multicast)"
	);
	for(const std::string& uri : LoadNetworkHistory())
		combo_network_uri.append(uri);
	combo_network_uri.get_entry()->signal_activate().connect([this]() {
		if(!tglbtn_network_connect.get_active())
			tglbtn_network_connect.set_active(true);
	});

	tglbtn_network_connect.set_label("Connect");
	tglbtn_network_connect.signal_toggled().connect(sigc::mem_fun(*this, &DABlinGTK::on_tglbtn_network_connect));

	// Network is the default source, regardless of whether a DAB live source is available
	tglbtn_source_dab.set_active(false);
	tglbtn_source_network.set_active(true);
	source_stack.set_visible_child("network");

	frame_label_ensemble.set_label("Ensemble");
	frame_label_ensemble.add(label_ensemble);
	label_ensemble.set_halign(Gtk::ALIGN_START);
	label_ensemble.set_padding(WIDGET_SPACE, WIDGET_SPACE);
	// fixed width for "label (short_label) - EId": 16 + " (" + 8 + ")" + " - " + 4 chars, plus a cosmetic margin
	label_ensemble.set_width_chars(16 + 2 + 8 + 1 + 3 + 4 + 4);


	frame_services_table.set_label("Services details");
	frame_services_table.set_hexpand(true);

	services_table_liststore = Gtk::ListStore::create(services_table_cols);
	services_table_liststore->set_default_sort_func(sigc::mem_fun(*this, &DABlinGTK::ServicesTableSlotCompare));
	services_table_liststore->set_sort_column(Gtk::TreeSortable::DEFAULT_SORT_COLUMN_ID, Gtk::SORT_ASCENDING);

	services_table.set_model(services_table_liststore);
	services_table.set_headers_visible(true);
	services_table.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &DABlinGTK::on_services_table_selection_changed));
	// data services (e.g. EPG) are listed for CU accounting only - not selectable/playable
	services_table.get_selection()->set_select_function([this](const Glib::RefPtr<Gtk::TreeModel>& model, const Gtk::TreeModel::Path& path, bool) {
		return ((LISTED_SERVICE) (*model->get_iter(path))[services_table_cols.col_service]).is_audio;
	});

	static const char* services_table_column_titles[] =     {"SubChId", "SID", "ECC", "Label", "Short label", "CUs", "Protection", "Bit rate", "Format", "Samplerate", "Audio", "Language", "PTY"};
	static const int services_table_column_min_widths[] =   {      60,    50,     50,    100,            80,     90,          80,         70,      110,           80,      70,         90,  180};
	int services_table_min_width = 0;
	for(size_t i = 0; i < sizeof(services_table_column_titles) / sizeof(services_table_column_titles[0]); i++) {
		Gtk::CellRendererText *renderer = Gtk::manage(new Gtk::CellRendererText());
		int col_index = services_table.append_column(services_table_column_titles[i], *renderer) - 1;
		Gtk::TreeViewColumn *col = services_table.get_column(col_index);
		col->set_cell_data_func(*renderer, sigc::bind(sigc::mem_fun(*this, &DABlinGTK::RenderServiceDetailCell), (int) i));
		col->set_resizable(true);
		col->set_min_width(services_table_column_min_widths[i]);
		services_table_min_width += services_table_column_min_widths[i];

		// widest PTY string ("Children's programmes"/"Soft Rhythm and Blues") is 21 chars, plus a cosmetic margin
		if(std::string(services_table_column_titles[i]) == "PTY")
			renderer->property_width_chars() = 21 + 3;
	}

	services_table.set_has_tooltip(true);
	services_table.signal_query_tooltip().connect(sigc::mem_fun(*this, &DABlinGTK::on_services_table_query_tooltip));

	services_table_scroll.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
	services_table_scroll.set_vexpand(true);
	services_table_scroll.add(services_table);
	// reserve enough room for all columns (unscrolled) and 15 services (scrollable beyond that)
	services_table_scroll.set_min_content_width(services_table_min_width + 40);	// + scrollbar/border margin
	services_table_scroll.set_min_content_height(15 * 26 + 30);	// ~26px/row + header

	services_table_box.set_orientation(Gtk::ORIENTATION_VERTICAL);
	services_table_box.pack_start(services_table_scroll, true, true);
	services_table_box.pack_start(cu_bar, false, false);
	cu_bar.signal_service_clicked.connect(sigc::mem_fun(*this, &DABlinGTK::on_cu_bar_service_clicked));
	frame_services_table.add(services_table_box);
	frame_services_table.set_vexpand(true);

	tglbtn_record.set_image_from_icon_name("media-record");
	tglbtn_record.signal_clicked().connect(sigc::mem_fun(*this, &DABlinGTK::on_tglbtn_record));
	tglbtn_record.set_sensitive(false);
	tglbtn_record.set_tooltip_text("Record service");
	tglbtn_record.set_halign(Gtk::ALIGN_START);
	tglbtn_record.set_margin_top(10);	// align with the frames to the left, whose label strip pushes their content down

	label_record.set_halign(Gtk::ALIGN_START);
	label_record.set_padding(WIDGET_SPACE, WIDGET_SPACE);

	tglbtn_logs.set_label("Logs");
	tglbtn_logs.signal_clicked().connect(sigc::mem_fun(*this, &DABlinGTK::on_tglbtn_logs));
	tglbtn_logs.set_tooltip_text("Log of hot changes on the ensemble");
	tglbtn_logs.set_halign(Gtk::ALIGN_START);
	tglbtn_logs.set_margin_top(10);

	tglbtn_mute.set_image_from_icon_name("audio-volume-muted");
	tglbtn_mute.signal_clicked().connect(sigc::mem_fun(*this, &DABlinGTK::on_tglbtn_mute));
	tglbtn_mute.set_tooltip_text("Mute");
	tglbtn_mute.set_halign(Gtk::ALIGN_START);
	tglbtn_mute.set_margin_top(10);

	vlmbtn.set_value(1.0);
	vlmbtn.set_sensitive(ensemble_player->HasAudioVolumeControl());
	vlmbtn.signal_value_changed().connect(sigc::mem_fun(*this, &DABlinGTK::on_vlmbtn));
	vlmbtn.set_halign(Gtk::ALIGN_START);
	vlmbtn.set_margin_top(10);

	frame_label_dl.set_label("Dynamic Label");
	frame_label_dl.set_size_request(750, 50);
	frame_label_dl.set_sensitive(false);
	frame_label_dl.set_hexpand(true);
	frame_label_dl.add(label_dl);
	label_dl.set_halign(Gtk::ALIGN_START);
	label_dl.set_valign(Gtk::ALIGN_START);
	label_dl.set_padding(WIDGET_SPACE, WIDGET_SPACE);
	label_dl.set_selectable(true);	// enables text selection + the native right-click "Copy" menu

	frame_dl_plus.set_label("Dynamic Label Plus");
	frame_dl_plus.set_hexpand(true);
	frame_dl_plus.add(dl_plus_content);

	frame_spectrum.set_label("Spectrum");
	frame_spectrum.add(spectrum);

	frame_level_meter.set_label("Level");
	frame_level_meter.add(level_meter);

	frame_slideshow.set_label("Slideshow");
	frame_slideshow.add(slideshow_content);

	dl_plus_slideshow_box.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
	dl_plus_slideshow_box.set_spacing(WIDGET_SPACE);
	dl_plus_slideshow_box.pack_start(frame_dl_plus, true, true);
	dl_plus_slideshow_box.pack_start(frame_spectrum, false, false);
	dl_plus_slideshow_box.pack_start(frame_level_meter, false, false);
	dl_plus_slideshow_box.pack_start(frame_slideshow, false, false);

	lower_controls_box.set_orientation(Gtk::ORIENTATION_VERTICAL);
	lower_controls_box.set_spacing(WIDGET_SPACE);
	lower_controls_box.pack_start(frame_label_dl, false, false);
	lower_controls_box.pack_start(dl_plus_slideshow_box, false, false);
	// the level meter needs vexpand to fill its own frame, but that must not bubble up into
	// top_grid's row allocation - otherwise enlarging the window vertically splits the extra
	// space between "Services details" and this row, instead of only growing the former
	lower_controls_box.set_vexpand(false);

	frame_label_datetime.set_label("Local date/time");
	frame_label_datetime.set_sensitive(false);
	frame_label_datetime.add(label_datetime);
	label_datetime.set_halign(Gtk::ALIGN_START);
	label_datetime.set_padding(WIDGET_SPACE, WIDGET_SPACE);
	// fixed width for "YYYY-MM-DD, Www - HH:MM:SS": 10 + 2 + 3 + 3 + 8 chars, plus a cosmetic margin
	label_datetime.set_width_chars(10 + 2 + 3 + 3 + 8 + 4);

	frame_label_asu.set_label("Announcement support");
	frame_label_asu.set_sensitive(false);
	frame_label_asu.set_hexpand(true);
	frame_label_asu.add(label_asu);
	label_asu.set_halign(Gtk::ALIGN_START);
	label_asu.set_padding(WIDGET_SPACE, WIDGET_SPACE);

	progress_position.set_show_text();


	top_grid.set_column_spacing(WIDGET_SPACE);
	top_grid.set_row_spacing(WIDGET_SPACE);

	// kept in its own grid (not sharing columns with the much wider services table below), so growing
	// the window never redistributes extra width onto its columns and makes the buttons drift apart
	header_grid.set_column_spacing(WIDGET_SPACE);
	header_grid.set_row_spacing(WIDGET_SPACE);
	header_grid.set_halign(Gtk::ALIGN_START);

	header_grid.attach(frame_source, 0, 0, 1, 2);
	header_grid.attach_next_to(frame_label_ensemble, frame_source, Gtk::POS_RIGHT, 1, 2);
	header_grid.attach_next_to(frame_label_datetime, frame_label_ensemble, Gtk::POS_RIGHT, 1, 2);
	header_grid.attach_next_to(tglbtn_logs, frame_label_datetime, Gtk::POS_RIGHT, 1, 1);
	header_grid.attach_next_to(tglbtn_record, tglbtn_logs, Gtk::POS_RIGHT, 1, 1);
	header_grid.attach_next_to(label_record, tglbtn_record, Gtk::POS_RIGHT, 3, 1);
	header_grid.attach_next_to(tglbtn_mute, label_record, Gtk::POS_RIGHT, 1, 1);
	header_grid.attach_next_to(vlmbtn, tglbtn_mute, Gtk::POS_RIGHT, 1, 1);

	// add widgets
	add(top_grid);

	top_grid.attach(header_grid, 0, 0, 1, 1);
	top_grid.attach_next_to(frame_services_table, header_grid, Gtk::POS_BOTTOM, 1, 1);
	top_grid.attach_next_to(lower_controls_box, frame_services_table, Gtk::POS_BOTTOM, 1, 1);
	top_grid.attach_next_to(frame_label_asu, lower_controls_box, Gtk::POS_BOTTOM, 1, 1);
	top_grid.attach_next_to(progress_position, frame_label_asu, Gtk::POS_BOTTOM, 1, 1);

	show_all_children();
	progress_position.hide();	// invisible until progress updated
	source_stack.set_visible_child("network");	// show_all_children() re-shows both pages
}

void DABlinGTK::AddChannels() {
	if(options.displayed_channels.empty()) {
		// add all channels
		for(const dab_channels_t::value_type& dab_channel : dab_channels)
			AddChannel(dab_channel, options.gain);
	} else {
		// add specific channels
		string_vector_t channels = StringTools::SplitString(options.displayed_channels, ',');
		for(const std::string& channel : channels) {
			int gain = options.gain;
			string_vector_t parts = StringTools::SplitString(channel, ':');
			switch(parts.size()) {
			case 2:
				gain = strtol(parts[1].c_str(), nullptr, 0);
				// fall through
			case 1: {
				dab_channels_t::const_iterator it = dab_channels.find(parts[0]);
				if(it != dab_channels.cend())
					AddChannel(*it, gain);
				else
					fprintf(stderr, "DABlinGTK: The channel '%s' is not supported; ignoring!\n", parts[0].c_str());
				break; }
			default:
				fprintf(stderr, "DABlinGTK: The format of channel '%s' is not supported; ignoring!\n", channel.c_str());
				continue;
			}
		}
	}
}

void DABlinGTK::AddChannel(const dab_channels_t::value_type& dab_channel, int gain) {
	Gtk::ListStore::iterator row_it = combo_channels_liststore->append();
	Gtk::TreeModel::Row row = *row_it;
	row[combo_channels_cols.col_string] = dab_channel.first;
	row[combo_channels_cols.col_channel] = DAB_LIVE_SOURCE_CHANNEL(dab_channel.first, dab_channel.second, gain);

	if(dab_channel.first == options.initial_channel)
		initial_channel_it = row_it;
}

void DABlinGTK::SetService(const LISTED_SERVICE& service) {
	// (re)set labels/tooltips/record button
	if(!service.IsNone()) {
		std::string label = FICDecoder::ConvertLabelToUTF8(service.label, nullptr);

		set_title(label + " - DABlin");
		set_icon_name(tglbtn_record.get_active() ? "media-record" : "media-playback-start");

		tglbtn_record.set_sensitive(true);
	} else {
		set_title("DABlin");
		set_icon_name("media-playback-stop");

		tglbtn_record.set_sensitive(false);
	}

	UpdateAnnouncementSupport(service);

	// if the audio service changed, reset format/DL/slide + switch
	bool audio_service_changed = !ensemble_player->IsSameAudioService(service.audio_service);
	if(audio_service_changed) {
		// stop playback of old service
		if(options.rec_prebuffer_size_s > 0)
			ensemble_player->RemoveUntouchedStreamConsumer(this);
		ensemble_player->SetAudioService(AUDIO_SERVICE());

		pad_decoder->Reset();

		current_audio_format_subchid = AUDIO_SERVICE::subchid_none;
		services_table.queue_draw();

		frame_label_dl.set_sensitive(false);
		label_dl.set_label("");
		frame_label_dl.set_tooltip_text("");

		dl_plus_content.ClearDLPlusInfo();
		level_meter.Reset();
		spectrum.Reset();

		if(!service.HasSLS())
			slideshow_content.ClearSlide();

		{
			std::lock_guard<std::mutex> lock(rec_mutex);

			// clear prebuffer, if enabled
			if(options.rec_prebuffer_size_s > 0) {
				rec_prebuffer.clear();
				rec_prebuffer_filled_ms = 0;
			}

			UpdateRecStatus(!service.IsNone());
		}

		// start playback of new service
		ensemble_player->SetAudioService(service.audio_service);
		if(options.rec_prebuffer_size_s > 0)
			ensemble_player->AddUntouchedStreamConsumer(this);
	}

	// (re)init SLS on new service (having SLS) only once
	if(service.HasSLS() && (audio_service_changed || slideshow_content.IsEmptySlide())) {
		slideshow_content.AwaitSlide();
		pad_decoder->SetMOTAppType(service.sls_app_type);
	}
}

void DABlinGTK::UpdateAnnouncementSupport(const LISTED_SERVICE& service) {
	// reset, if no service/announcement support
	if(service.IsNone() || service.asu_flags == LISTED_SERVICE::asu_flags_none) {
		frame_label_asu.set_sensitive(false);
		frame_label_asu.set_tooltip_markup("");
		label_asu.set_label("");
		return;
	}

	// assemble text - iterate through all supported announcement types
	std::string ac_str;
	size_t count = 0;
	for(int type = 0; type < 16; type++) {
		uint16_t type_flag = 1 << type;
		if(!(service.asu_flags & type_flag))
			continue;

		// check all matching announcement clusters for active announcement
		std::string asw_color;
		for(const cids_t::value_type& cid : service.cids) {
			asw_clusters_t::const_iterator it = ensemble.asw_clusters.find(cid);
			if(it == ensemble.asw_clusters.cend() || !(it->second.asw_flags & type_flag))
				continue;

			if(it->second.subchid == service.audio_service.subchid) {
				asw_color = "yellow";
			} else {
				if(asw_color.empty())
					asw_color = "cyan";
			}
		}

		// regularly insert line break
		if(!ac_str.empty()) {
			ac_str += (count % 6 == 0) ? "\n" : " ";
			ac_str += "+ ";
		}
		count++;

		if(!asw_color.empty())
			ac_str += "<span bgcolor='" + asw_color + "'>";
		ac_str += FICDecoder::ConvertASuTypeToString(type);
		if(!asw_color.empty())
			ac_str += "</span>";
	}

	// assemble tooltip - iterate through all matching announcement clusters
	std::string ac_tooltip;
	char cid_string[5];
	for(const cids_t::value_type& cid : service.cids) {
		asw_clusters_t::const_iterator it = ensemble.asw_clusters.find(cid);
		bool present = it != ensemble.asw_clusters.cend();
		bool active = present && (service.asu_flags & it->second.asw_flags);

		ac_tooltip += ac_tooltip.empty() ? "Cluster(s): " : " / ";
		if(active)
			ac_tooltip += "<u>";
		snprintf(cid_string, sizeof(cid_string), "0x%02X", cid);
		ac_tooltip += std::string(cid_string);
		if(present)
			ac_tooltip += " (SubChId " + std::to_string(it->second.subchid) + ")";
		if(active)
			ac_tooltip += "</u>";
	}

	frame_label_asu.set_sensitive(true);
	frame_label_asu.set_tooltip_markup(ac_tooltip);
	label_asu.set_markup(ac_str);
}

void DABlinGTK::on_tglbtn_record() {
	if(tglbtn_record.get_active()) {
		// get start timestamp
		time_t start;
		{
			std::lock_guard<std::mutex> lock(rec_mutex);

			// use prebuffer start information, if available
			if(!rec_prebuffer.empty()) {
				start = rec_prebuffer.front().ts;
			} else {
				start = time(nullptr);
				if(start == (time_t) -1)
					perror("DABlinGTK: error while getting time for recording");
			}
		}
		struct tm* start_tm = localtime(&start);
		if(!start_tm)
			perror("DABlinGTK: error while getting local time");
		char start_string[22];
		strftime(start_string, sizeof(start_string), "%F - %H-%M-%S", start_tm);

		// get station name
		LISTED_SERVICE service;	// default: none
		Gtk::TreeModel::iterator row_it = services_table.get_selection()->get_selected();
		if(row_it)
			service = (LISTED_SERVICE) (*row_it)[services_table_cols.col_service];
		std::string label = FICDecoder::ConvertLabelToUTF8(service.label, nullptr);

		// escape forbidden '/' character
		std::string label_cleaned = label;
		for(char& c : label_cleaned)
			if(c == '/')
				c = '_';

		std::string new_rec_filename = options.recordings_path + "/" + std::string(start_string) + " - " + label_cleaned + "." + ensemble_player->GetUntouchedStreamFileExtension();
		FILE* new_rec_file = fopen(new_rec_filename.c_str(), "wb");
		if(new_rec_file) {
			// disable channel/service switch
			combo_channels.set_sensitive(false);	// parent frame already non-sensitive, if channels not available
			btn_channels_stop.set_sensitive(false);	// parent frame already non-sensitive, if channels not available
			services_table.set_sensitive(false);
			cu_bar.set_sensitive(false);

			set_icon_name("media-record");
			fprintf(stderr, "DABlinGTK: recording started into '%s'\n", new_rec_filename.c_str());

			{
				std::lock_guard<std::mutex> lock(rec_mutex);

				rec_file = new_rec_file;
				rec_filename = new_rec_filename;

				// write prebuffer
				for(const RecSample& s : rec_prebuffer)
					if(fwrite(&s.data[0], s.data.size(), 1, rec_file) != 1)
						perror("DABlinGTK: error while writing untouched stream prebuffer to file");
				rec_duration_ms = rec_prebuffer_filled_ms;

				// clear prebuffer
				rec_prebuffer.clear();
				rec_prebuffer_filled_ms = 0;

				UpdateRecStatus(true);
			}

			if(options.rec_prebuffer_size_s == 0)
				ensemble_player->AddUntouchedStreamConsumer(this);
		} else {
			int fopen_errno = errno;	// save before overwrite
			tglbtn_record.set_active(false);

			Gtk::MessageDialog hint(*this, "Could not start recording!", false, Gtk::MESSAGE_ERROR);
			hint.set_title("DABlinGTK");
			hint.set_secondary_text(strerror(fopen_errno), false);
			hint.run();
		}
	} else {
		std::lock_guard<std::mutex> lock(rec_mutex);

		// only stop recording, if active (handles error on recording start)
		if(rec_file) {
			if(options.rec_prebuffer_size_s == 0)
				ensemble_player->RemoveUntouchedStreamConsumer(this);

			fclose(rec_file);
			rec_file = nullptr;

			set_icon_name("media-playback-start");
			fprintf(stderr, "DABlinGTK: recording stopped\n");
			UpdateRecStatus(true);

			// enable channel/service switch
			combo_channels.set_sensitive(true);		// parent frame already non-sensitive, if channels not available
			btn_channels_stop.set_sensitive(true);	// parent frame already non-sensitive, if channels not available
			services_table.set_sensitive(true);
			cu_bar.set_sensitive(true);
		}
	}
}

void DABlinGTK::ProcessUntouchedStream(const uint8_t* data, size_t len, size_t duration_ms) {
	std::lock_guard<std::mutex> lock(rec_mutex);

	// if recording in progress
	if(rec_file) {
		long int rec_duration_ms_old = rec_duration_ms;

		// write sample
		if(fwrite(data, len, 1, rec_file) != 1)
			perror("DABlinGTK: error while writing untouched stream sample to file");
		rec_duration_ms += duration_ms;

		// update status only on seconds change
		if(rec_duration_ms / 1000 != rec_duration_ms_old / 1000)
			DoRecStatusUpdate(true);
	} else {
		// if prebuffer enabled
		if(options.rec_prebuffer_size_s > 0) {
			long int rec_prebuffer_ms_old = rec_prebuffer_filled_ms;

			// append sample
			rec_prebuffer.emplace_back(data, len, duration_ms);
			rec_prebuffer_filled_ms += duration_ms;

			// remove samples while needed
			while(rec_prebuffer_filled_ms - rec_prebuffer.front().duration_ms >= options.rec_prebuffer_size_s * 1000) {
				rec_prebuffer_filled_ms -= rec_prebuffer.front().duration_ms;
				rec_prebuffer.pop_front();
			}

			// update status only on seconds change
			if(rec_prebuffer_filled_ms / 1000 != rec_prebuffer_ms_old / 1000)
				DoRecStatusUpdate(true);
		}
	}
}

void DABlinGTK::DoRecStatusUpdateEmitted() {
	std::lock_guard<std::mutex> lock(rec_mutex);
	UpdateRecStatus(do_rec_status_update.Pop());
}

void DABlinGTK::UpdateRecStatus(bool decoding) {
	// mutex must already be locked!

	// if recording in progress
	if(rec_file) {
		label_record.set_label(StringTools::MsToTimecode(rec_duration_ms));
		label_record.set_sensitive(true);
		label_record.set_tooltip_text("File name: \"" + rec_filename + "\"");
	} else {
		// if prebuffer enabled and currently decoding
		if(options.rec_prebuffer_size_s > 0 && decoding) {
			label_record.set_label(StringTools::MsToTimecode(rec_prebuffer_filled_ms));
			label_record.set_sensitive(false);
			label_record.set_tooltip_text("Prebuffer: " + StringTools::MsToTimecode(options.rec_prebuffer_size_s * 1000));
		} else {
			label_record.set_label("");
			label_record.set_tooltip_text("");
		}
	}
}


void DABlinGTK::on_tglbtn_mute() {
	ensemble_player->SetAudioMute(tglbtn_mute.get_active());
}

void DABlinGTK::on_vlmbtn(double value) {
	ensemble_player->SetAudioVolume(value);

	// disable mute, if needed
	if(tglbtn_mute.get_active())
		tglbtn_mute.clicked();
}

void DABlinGTK::on_tglbtn_logs() {
	if(tglbtn_logs.get_active()) {
		logs_window.AlignToParent();
		logs_window.show();
	} else {
		logs_window.hide();
	}
}

bool DABlinGTK::on_window_delete_event(GdkEventAny* /*any_event*/) {
	// prevent exit while recording
	if(tglbtn_record.get_active()) {
		Gtk::MessageDialog hint(*this, "Cannot exit while recording!", false, Gtk::MESSAGE_ERROR);
		hint.set_title("DABlinGTK");
		hint.set_secondary_text("The recording has to be stopped first.", false);
		hint.run();
		return true;
	} else {
		return false;
	}
}

bool DABlinGTK::HandleKeyPressEvent(GdkEventKey* key_event) {
	// events without Shift/Control/Alt/Win
	if((key_event->state & (Gdk::SHIFT_MASK | Gdk::CONTROL_MASK | Gdk::MOD1_MASK | Gdk::SUPER_MASK)) == 0) {
		switch(key_event->keyval) {
		case GDK_KEY_m:
		case GDK_KEY_M:
			// toggle mute
			tglbtn_mute.clicked();
			return true;
		case GDK_KEY_r:
		case GDK_KEY_R:
			// toggle record, if allowed
			if(tglbtn_record.get_sensitive())
				tglbtn_record.clicked();
			return true;
		}

		// try to switch service (1 to 10, -/+)
		int new_index;
		if(CheckForIndexKey(key_event, GetSelectedServiceIndex(), new_index)) {
			TrySwitchService(new_index);
			return true;
		}
	}

	// events without Shift/Control/Win, but with Alt
	if((key_event->state & (Gdk::SHIFT_MASK | Gdk::CONTROL_MASK | Gdk::SUPER_MASK)) == 0 && (key_event->state & Gdk::MOD1_MASK)) {
		// try to switch service (11 to 20)
		int new_index;
		if(CheckForIndexKey(key_event, -1, new_index)) {
			TrySwitchService(new_index + 10);
			return true;
		}
	}

	// events without Shift/Alt/Win, but with Control
	if((key_event->state & (Gdk::SHIFT_MASK | Gdk::MOD1_MASK | Gdk::SUPER_MASK)) == 0 && (key_event->state & Gdk::CONTROL_MASK)) {
		// try to switch channel (1 to 10, -/+)
		int new_index;
		if(CheckForIndexKey(key_event, combo_channels.get_active_row_number(), new_index)) {
			TrySwitch(combo_channels, combo_channels_liststore, new_index);
			return true;
		}

		switch(key_event->keyval) {
		case GDK_KEY_c:
		case GDK_KEY_C: {
			// copy DL to clipboard, if not empty
			std::string dl = label_dl.get_label();
			if(!dl.empty())
				Gtk::Clipboard::get()->set_text(dl);
			return true; }
		case GDK_KEY_space:
			// stop/resume decoding the current channel/service, if allowed
			if(btn_channels_stop.get_sensitive())
				btn_channels_stop.clicked();
			return true;
		}
	}

	// events without Shift/Win, but with Control/Alt
	if((key_event->state & (Gdk::SHIFT_MASK | Gdk::SUPER_MASK)) == 0 && (key_event->state & Gdk::CONTROL_MASK) && (key_event->state & Gdk::MOD1_MASK)) {
		// try to switch channel (11 to 20)
		int new_index;
		if(CheckForIndexKey(key_event, -1, new_index)) {
			TrySwitch(combo_channels, combo_channels_liststore, new_index + 10);
			return true;
		}
	}

	return false;
}

bool DABlinGTK::CheckForIndexKey(GdkEventKey* key_event, int old_index, int& new_index) {
	switch(key_event->keyval) {
	case GDK_KEY_1:
	case GDK_KEY_KP_1:
		new_index = 0;
		return true;
	case GDK_KEY_2:
	case GDK_KEY_KP_2:
		new_index = 1;
		return true;
	case GDK_KEY_3:
	case GDK_KEY_KP_3:
		new_index = 2;
		return true;
	case GDK_KEY_4:
	case GDK_KEY_KP_4:
		new_index = 3;
		return true;
	case GDK_KEY_5:
	case GDK_KEY_KP_5:
		new_index = 4;
		return true;
	case GDK_KEY_6:
	case GDK_KEY_KP_6:
		new_index = 5;
		return true;
	case GDK_KEY_7:
	case GDK_KEY_KP_7:
		new_index = 6;
		return true;
	case GDK_KEY_8:
	case GDK_KEY_KP_8:
		new_index = 7;
		return true;
	case GDK_KEY_9:
	case GDK_KEY_KP_9:
		new_index = 8;
		return true;
	case GDK_KEY_0:
	case GDK_KEY_KP_0:
		new_index = 9;
		return true;
	case GDK_KEY_minus:
	case GDK_KEY_KP_Subtract:
		if(old_index == -1)
			break;
		new_index = old_index - 1;
		return true;
	case GDK_KEY_plus:
	case GDK_KEY_KP_Add:
		if(old_index == -1)
			break;
		new_index = old_index + 1;
		return true;
	}
	return false;
}

void DABlinGTK::TrySwitch(Gtk::ComboBox& combo, Glib::RefPtr<Gtk::ListStore>& combo_liststore, int index) {
	// switch, if allowed and index valid
	if(combo.is_sensitive() && index >= 0 && index < (signed) combo_liststore->children().size())
		combo.set_active(index);
}

int DABlinGTK::GetSelectedServiceIndex() {
	Gtk::TreeModel::iterator row_it = services_table.get_selection()->get_selected();
	if(!row_it)
		return -1;

	Gtk::TreeModel::Path path = services_table_liststore->get_path(row_it);
	return path.empty() ? -1 : path[0];
}

void DABlinGTK::TrySwitchService(int index) {
	// switch, if allowed and index valid
	if(!services_table.is_sensitive() || index < 0 || index >= (signed) services_table_liststore->children().size())
		return;

	Gtk::TreeModel::Path path;
	path.push_back(index);
	Gtk::TreeModel::iterator row_it = services_table_liststore->get_iter(path);
	if(row_it)
		services_table.get_selection()->select(row_it);
}

void DABlinGTK::UpdateCUBar() {
	std::vector<LISTED_SERVICE> services;
	for(const Gtk::TreeModel::Row& row : services_table_liststore->children())
		services.push_back((LISTED_SERVICE) row[services_table_cols.col_service]);
	cu_bar.SetServices(services);
	cu_bar.SetSubchannels(known_subchannels);

	int selected_subchid = AUDIO_SERVICE::subchid_none;
	Gtk::TreeModel::iterator current_it = services_table.get_selection()->get_selected();
	if(current_it)
		selected_subchid = ((LISTED_SERVICE) (*current_it)[services_table_cols.col_service]).audio_service.subchid;
	cu_bar.SetSelectedSubchId(selected_subchid);
}

bool DABlinGTK::HandleConfigureEvent(GdkEventConfigure* /*configure_event*/) {
	// move together with the children window
	if(logs_window.get_visible())
		logs_window.AlignToParent();
	return false;
}

void DABlinGTK::EnsembleUpdateProgressEmitted() {
//	fprintf(stderr, "### EnsembleUpdateProgressEmitted\n");

	ENSEMBLE_PROGRESS progress = ensemble_update_progress.Pop();

	progress_position.set_fraction(progress.value);
	progress_position.set_text(progress.text);
	if(!progress_position.get_visible())
		progress_position.show();
}

void DABlinGTK::EnsembleChangeFormatEmitted() {
//	fprintf(stderr, "### EnsembleChangeFormatEmitted\n");

	current_audio_format = ensemble_change_format.Pop();

	// remember which sub-channel this format applies to, so the services table can show it for that row only
	Gtk::TreeModel::iterator current_it = services_table.get_selection()->get_selected();
	current_audio_format_subchid = current_it ? ((LISTED_SERVICE) (*current_it)[services_table_cols.col_service]).audio_service.subchid : AUDIO_SERVICE::subchid_none;
	services_table.queue_draw();
}

void DABlinGTK::EnsembleAudioLevelEmitted() {
	level_meter.SetLevels(ensemble_audio_level.Pop());
}

void DABlinGTK::EnsembleAudioSpectrumEmitted() {
	spectrum.SetSpectrum(ensemble_audio_spectrum.Pop());
}

void DABlinGTK::FICChangeEnsemble(const FIC_ENSEMBLE& ensemble) {
	fic_change_ensemble.PushAndEmit(ensemble);

	// update LTO for date/time + show
	if(dt_lto != ensemble.lto) {
		dt_lto = ensemble.lto;
//		fprintf(stderr, "### LTO updated\n");
		ShowDateTime(false);
	}
}

void DABlinGTK::FICChangeEnsembleEmitted() {
//	fprintf(stderr, "### FICChangeEnsembleEmitted\n");

	ensemble = fic_change_ensemble.Pop();

	std::string charset_name;
	std::string label_plain = FICDecoder::ConvertLabelToUTF8(ensemble.label, &charset_name);
	std::string short_label_plain = FICDecoder::DeriveShortLabelUTF8(label_plain, ensemble.label.short_label_mask);

	std::string tooltip_text =
			"Short label: \"" + short_label_plain + "\"\n"
			"Label charset: " + charset_name + "\n"
			"EId: " + StringTools::IntToHex(ensemble.eid, 4).substr(2) + "\n"
			"Alarm flag: " + (ensemble.al_flag ? "true" : "false");
	if(ensemble.ecc != FIC_ENSEMBLE::ecc_none) {
		tooltip_text += "\n" "ECC: " + StringTools::IntToHex(ensemble.ecc, 2).substr(2);
		const char *country = LookupECCCountry(ensemble.ecc, ensemble.eid >> 12);
		if(country)
			tooltip_text += " (" + std::string(country) + ")";
	}
	if(ensemble.lto != FIC_ENSEMBLE::lto_none)
		tooltip_text += "\n" "LTO: " + FICDecoder::ConvertLTOToString(ensemble.lto);
	if(ensemble.inter_table_id != FIC_ENSEMBLE::inter_table_id_none)
		tooltip_text += "\n" "International table ID: " + StringTools::IntToHex(ensemble.inter_table_id, 2) + " (" + FICDecoder::ConvertInterTableIDToString(ensemble.inter_table_id) + ")";

	// label (short label) - EId, without the "0x" prefix
	std::string label =
			Glib::Markup::escape_text(label_plain) +
			" (" + Glib::Markup::escape_text(short_label_plain) + ")" +
			" - " + StringTools::IntToHex(ensemble.eid, 4).substr(2);

	// indicate active alarm announcement
	if(ensemble.al_flag) {
		asw_clusters_t::const_iterator cl_it = ensemble.asw_clusters.find(0xFF);
		if(cl_it != ensemble.asw_clusters.end() && (cl_it->second.asw_flags & 0x01)) {
			label += " <span color='white' bgcolor='red'><b> Alarm </b></span>";
			tooltip_text += "\n" "Alarm SubChId: " + std::to_string(cl_it->second.subchid);
		}
	}

	label_ensemble.set_markup(label);
	frame_label_ensemble.set_tooltip_text(tooltip_text);
}

void DABlinGTK::FICChangeServiceEmitted() {
//	fprintf(stderr, "### FICChangeServiceEmitted\n");

	LISTED_SERVICE new_service = fic_change_service.Pop();
	std::string label = FICDecoder::ConvertLabelToUTF8(new_service.label, nullptr);

	bool add_new_row;
	Gtk::ListStore::iterator row_it = UpdateServicesTableRow(new_service, &add_new_row);

	if(add_new_row) {
		// if first found service requested, adopt service params (for possible later changes)
		if(options.initial_first_found_service) {
			switch_service_sid = new_service.sid;
			switch_service_scids = new_service.scids;
			options.initial_first_found_service = false;
		}

		// set (initial) service
		if(label == switch_service_label || (new_service.sid == switch_service_sid && new_service.scids == switch_service_scids))
			services_table.get_selection()->select(row_it);
	} else {
		// set (updated) service
		Gtk::TreeModel::iterator current_it = services_table.get_selection()->get_selected();
		if(current_it && current_it == row_it)
			SetService(new_service);
	}

	RebuildDataSubchannelRows();
	UpdateCUBar();
}

void DABlinGTK::FICRemoveServiceEmitted() {
	fic_service_id_t id = fic_remove_service.Pop();

	Gtk::ListStore::Children children = services_table_liststore->children();
	Gtk::ListStore::iterator row_it = std::find_if(
			children.begin(), children.end(),
			[&](const Gtk::TreeModel::Row& row)->bool {
				const LISTED_SERVICE& ls = (LISTED_SERVICE) row[services_table_cols.col_service];
				return ls.sid == id.first && ls.scids == id.second;
			}
	);
	if(row_it == children.end())
		return;

	// if the removed service is currently selected, stop playback and deselect it first
	Gtk::TreeModel::iterator current_it = services_table.get_selection()->get_selected();
	if(current_it && current_it == row_it)
		services_table.get_selection()->unselect_all();

	services_table_liststore->erase(row_it);

	RebuildDataSubchannelRows();
	UpdateCUBar();
}

void DABlinGTK::FICChangeSubchannelEmitted() {
	std::pair<int,FIC_SUBCHANNEL> change = fic_change_subchannel.Pop();
	known_subchannels[change.first] = change.second;
	RebuildDataSubchannelRows();
	UpdateCUBar();
}

void DABlinGTK::FICRemoveSubchannelEmitted() {
	known_subchannels.erase(fic_remove_subchannel.Pop());
	RebuildDataSubchannelRows();
	UpdateCUBar();
}

void DABlinGTK::FICChangeDataServiceSIDEmitted() {
	std::pair<int,uint32_t> change = fic_change_data_service_sid.Pop();
	known_data_service_sids[change.first] = change.second;
	RebuildDataSubchannelRows();
	UpdateCUBar();
}

void DABlinGTK::FICRemoveDataServiceSIDEmitted() {
	known_data_service_sids.erase(fic_remove_data_service_sid.Pop());
	RebuildDataSubchannelRows();
	UpdateCUBar();
}

void DABlinGTK::FICLogMessageEmitted() {
	logs_window.AppendLogMessage(fic_log_message.Pop());
}

void DABlinGTK::FICChangeUTCDateTime(const FIC_DAB_DT& utc_dt) {
	// (re)sync to DAB date/time + update/show/reschedule
	utc_dt_next = utc_dt;
	dt_update = std::chrono::steady_clock::now();
//	fprintf(stderr, "### time synced\n");

	if(dt_lto != FIC_ENSEMBLE::lto_none)
		DoDateTimeSync(FICDecoder::ConvertDateTimeToString(utc_dt, dt_lto, true));

	ShowDateTime(true);
}

void DABlinGTK::ShowDateTime(bool scheduled) {
	// if scheduled, update
	if(scheduled)
		utc_dt_curr = utc_dt_next;

	// if UTC date/time and LTO set, show, if output changed
	if(!utc_dt_curr.IsNone() && dt_lto != FIC_ENSEMBLE::lto_none) {
		std::string dt_str = FICDecoder::ConvertDateTimeToString(utc_dt_curr, dt_lto, false);
//		fprintf(stderr, "### time: %s\n", FICDecoder::ConvertDateTimeToString(utc_dt_curr, dt_lto, true).c_str());
		if(dt_str_prev != dt_str) {
			dt_str_prev = dt_str;
//			fprintf(stderr, "### ----- time displayed: %s\n", dt_str.c_str());
			DoDateTimeUpdate(dt_str);
		}
	}

	// if scheduled, (re)schedule
	if(scheduled) {
		if(!utc_dt_next.IsMsNone()) {
			// long form
			dt_update += std::chrono::milliseconds(1000 - utc_dt_next.ms);
			utc_dt_next.dt.tm_sec++;
			utc_dt_next.ms = 0;
		} else {
			// short form
			dt_update += std::chrono::minutes(1);
			utc_dt_next.dt.tm_min++;
		}
		if(mktime(&utc_dt_next.dt) == (time_t) -1)
			throw std::runtime_error("DABlinGTK: error while normalizing date/time");
	}
}

void DABlinGTK::EnsembleDoRegularWork() {
	// if UTC date/time set and scheduled time reached, update/show/reschedule
	if(!utc_dt_curr.IsNone() && std::chrono::steady_clock::now() >= dt_update)
		ShowDateTime(true);

	// detect services hot-removed from the ensemble (no longer listed in FIG 0/2)
	fic_decoder->CheckServiceTimeouts();
}

void DABlinGTK::DoDateTimeUpdateEmitted() {
	// display updated date/time
	frame_label_datetime.set_sensitive(true);
	label_datetime.set_label(do_datetime_update.Pop());
}

void DABlinGTK::DoDateTimeSyncEmitted() {
	// display synced date/time
	frame_label_datetime.set_sensitive(true);
	frame_label_datetime.set_tooltip_text("Last synced date/time: " + do_datetime_sync.Pop());
}

void DABlinGTK::FICDiscardedFIB() {
	fprintf(stderr, "\x1B[33m" "(FIB)" "\x1B[0m" " ");
}

void DABlinGTK::EnsureEnsemblePlayer(const std::string& format) {
	if(options.source_format == format)
		return;

	// preserve mute/volume across the swap
	bool mute = tglbtn_mute.get_active();
	double volume = vlmbtn.get_value();
	delete ensemble_player;

	options.source_format = format;
	if(format == EnsembleSource::FORMAT_ETI)
		ensemble_player = new ETIPlayer(audio_output_type, options.disable_int_catch_up, this);
	else
		ensemble_player = new EDIPlayer(audio_output_type, options.disable_int_catch_up, this);

	vlmbtn.set_sensitive(ensemble_player->HasAudioVolumeControl());
	ensemble_player->SetAudioMute(mute);
	ensemble_player->SetAudioVolume(volume);
}

void DABlinGTK::StopEnsembleSource() {
	if(ensemble_source) {
		ensemble_source->DoExit();
		ensemble_source_thread.join();
		delete ensemble_source;
		ensemble_source = nullptr;
	}

	EnsembleResetFIC();
	services_table_liststore->clear();	// TODO: prevent on_services_table_selection_changed() being called for each deleted row
	logs_window.Clear();
	known_subchannels.clear();
	known_data_service_sids.clear();
	UpdateCUBar();
	ensemble_player->StopAudio();
	label_ensemble.set_label("");
	frame_label_ensemble.set_tooltip_text("");

	frame_label_datetime.set_sensitive(false);
	frame_label_datetime.set_tooltip_text("");
	label_datetime.set_label("");

	utc_dt_curr = FIC_DAB_DT();
	dt_lto = FIC_ENSEMBLE::lto_none;
	dt_str_prev = "";
}

void DABlinGTK::on_source_type_changed(bool dab) {
	source_stack.set_visible_child(dab ? "dab" : "network");

	if(dab) {
		// leaving Network - disconnect if currently connected
		if(tglbtn_network_connect.get_active())
			tglbtn_network_connect.set_active(false);
	} else {
		// leaving DAB - stop the current channel, if any
		if(combo_channels.get_active_row_number() != -1)
			combo_channels.unset_active();
	}
}

void DABlinGTK::on_combo_channels() {
	btn_channels_stop.set_sensitive(true);

	// defensive: should be unreachable now that DAB/Network are mutually exclusive
	// stack pages, but cheap insurance against any programmatic path that bypasses it
	if(tglbtn_network_connect.get_active())
		tglbtn_network_connect.set_active(false);

	StopEnsembleSource();

	// apply
	Gtk::TreeModel::iterator row_it = combo_channels.get_active();
	if(combo_channels_liststore->iter_is_valid(row_it)) {
		Gtk::TreeModel::Row row = *row_it;
		DAB_LIVE_SOURCE_CHANNEL channel = row[combo_channels_cols.col_channel];

		combo_channels.set_tooltip_text(
				"Center frequency: " + std::to_string(channel.freq) + " kHz\n"
				"Gain: " + channel.GainToString()
		);

		// prevent re-use of switch service params next time
		if(!switch_service_applied) {
			switch_service_applied = true;
		} else {
			switch_service_label = "";
			switch_service_sid = LISTED_SERVICE::sid_none;
			switch_service_scids = LISTED_SERVICE::scids_none;
		}

		EnsureEnsemblePlayer(EnsembleSource::FORMAT_ETI);

		if(options.dab_live_source_type == DABLiveETISource::TYPE_ETI_CMDLINE)
			ensemble_source = new EtiCmdlineETISource(options.dab_live_source_binary, channel, this);
		else
			ensemble_source = new DAB2ETIETISource(options.dab_live_source_binary, channel, this);
		ensemble_source_thread = std::thread(&EnsembleSource::Main, ensemble_source);

		btn_channels_stop.set_image_from_icon_name("media-playback-stop");
	} else {
		combo_channels.set_tooltip_text("");
		btn_channels_stop.set_image_from_icon_name("media-playback-start");
	}
}

void DABlinGTK::ShowNetworkConnectError(const std::string& message) {
	Gtk::MessageDialog dialog(*this, message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
	dialog.set_title("Connection error");
	dialog.run();
}

void DABlinGTK::AddToNetworkHistory(const std::string& uri) {
	std::vector<std::string> history = LoadNetworkHistory();
	history.erase(std::remove(history.begin(), history.end(), uri), history.end());
	history.insert(history.begin(), uri);
	if(history.size() > NETWORK_HISTORY_MAX_ENTRIES)
		history.resize(NETWORK_HISTORY_MAX_ENTRIES);
	SaveNetworkHistory(history);

	combo_network_uri.remove_all();
	for(const std::string& h : history)
		combo_network_uri.append(h);
	combo_network_uri.get_entry()->set_text(uri);
}

void DABlinGTK::on_tglbtn_network_connect() {
	if(tglbtn_network_connect.get_active()) {
		std::string uri = combo_network_uri.get_active_text();

		static const std::string PREFIX_TCP = "tcp://";
		static const std::string PREFIX_UDP = "udp://";

		bool is_tcp;
		std::string rest;
		if(uri.compare(0, PREFIX_TCP.size(), PREFIX_TCP) == 0) {
			is_tcp = true;
			rest = uri.substr(PREFIX_TCP.size());
		} else if(uri.compare(0, PREFIX_UDP.size(), PREFIX_UDP) == 0) {
			is_tcp = false;
			rest = uri.substr(PREFIX_UDP.size());
		} else {
			ShowNetworkConnectError("Unsupported URI - use tcp://host:port or udp://host:port");
			tglbtn_network_connect.set_active(false);
			return;
		}

		size_t colon_pos = rest.find_last_of(':');
		std::string host = colon_pos == std::string::npos ? "" : rest.substr(0, colon_pos);
		std::string port_str = colon_pos == std::string::npos ? "" : rest.substr(colon_pos + 1);

		int port = -1;
		if(!port_str.empty()) {
			try {
				size_t idx;
				port = std::stoi(port_str, &idx);
				if(idx != port_str.size())
					port = -1;
			} catch(const std::exception&) {
				port = -1;
			}
		}
		if(port <= 0 || port > 65535) {
			ShowNetworkConnectError("Invalid or missing port in URI '" + uri + "'");
			tglbtn_network_connect.set_active(false);
			return;
		}
		if(is_tcp && host.empty()) {
			ShowNetworkConnectError("tcp:// requires a host");
			tglbtn_network_connect.set_active(false);
			return;
		}

		AddToNetworkHistory(uri);

		// take over from whatever was running before (initial CLI source, or a DAB-live channel)
		StopEnsembleSource();
		EnsureEnsemblePlayer(EnsembleSource::FORMAT_EDI);

		if(is_tcp)
			ensemble_source = new EDITCPSource(host, port, this);
		else
			ensemble_source = new EDIUDPSource(host, port, this);
		ensemble_source_thread = std::thread(&EnsembleSource::Main, ensemble_source);

		combo_network_uri.set_sensitive(false);
		tglbtn_network_connect.set_label("Disconnect");
	} else {
		StopEnsembleSource();

		combo_network_uri.set_sensitive(true);
		tglbtn_network_connect.set_label("Connect");
	}
}

void DABlinGTK::on_btn_channels_stop() {
	if(combo_channels.get_active_row_number() != -1) {
		// save state + stop
		Gtk::TreeModel::iterator row_it = services_table.get_selection()->get_selected();
		if(row_it) {
			LISTED_SERVICE service = (LISTED_SERVICE) (*row_it)[services_table_cols.col_service];
			resume_service_sid = service.sid;
			resume_service_scids = service.scids;
		} else {
			resume_service_sid = LISTED_SERVICE::sid_none;
			resume_service_scids = LISTED_SERVICE::scids_none;
		}

		resume_channel_it = combo_channels.get_active();
		combo_channels.unset_active();
	} else {
		// load state + resume
		switch_service_label = "";
		switch_service_sid = resume_service_sid;
		switch_service_scids = resume_service_scids;
		switch_service_applied = false;

		combo_channels.set_active(resume_channel_it);
	}
}

void DABlinGTK::on_services_table_selection_changed() {
	LISTED_SERVICE service;	// default: none
	Gtk::TreeModel::iterator row_it = services_table.get_selection()->get_selected();
	if(row_it)
		service = (LISTED_SERVICE) (*row_it)[services_table_cols.col_service];
	SetService(service);

	cu_bar.SetSelectedSubchId(service.audio_service.subchid);
}

void DABlinGTK::on_cu_bar_service_clicked(uint16_t sid, int scids) {
	Gtk::ListStore::Children children = services_table_liststore->children();
	Gtk::ListStore::iterator row_it = std::find_if(
			children.begin(), children.end(),
			[&](const Gtk::TreeModel::Row& row)->bool {
				const LISTED_SERVICE& ls = (LISTED_SERVICE) row[services_table_cols.col_service];
				return ls.sid == sid && ls.scids == scids;
			}
	);
	if(row_it != children.end())
		services_table.get_selection()->select(row_it);
}

void DABlinGTK::PADChangeDynamicLabelEmitted() {
//	fprintf(stderr, "### PADChangeDynamicLabelEmitted\n");

	DL_STATE dl = pad_change_dynamic_label.Pop();

	// consider clear display command
	if(dl.charset != -1) {
		std::string charset_name;
		std::string label = CharsetTools::ConvertTextToUTF8(&dl.raw[0], dl.raw.size(), dl.charset, false, &charset_name);

		// skip unsupported charsets
		if(!charset_name.empty()) {
			frame_label_dl.set_sensitive(true);
			label_dl.set_label(label);
			frame_label_dl.set_tooltip_text("Charset: " + charset_name);
		}
	} else {
		frame_label_dl.set_sensitive(true);
		label_dl.set_label("");
		frame_label_dl.set_tooltip_text("");
	}

	// if present, forward DL Plus updates
	if(!dl.dl_plus_objects.empty())
		dl_plus_content.UpdateDLPlusInfo(dl);
}

void DABlinGTK::PADChangeSlideEmitted() {
//	fprintf(stderr, "### PADChangeSlideEmitted\n");

	slideshow_content.UpdateSlide(pad_change_slide.Pop());
}

void DABlinGTK::PADFileProgressEmitted() {
//	fprintf(stderr, "### PADFileProgressEmitted\n");

	slideshow_content.UpdateFileProgress(pad_file_progress.Pop());
}

void DABlinGTK::PADLengthError(size_t /*announced_xpad_len*/, size_t /*xpad_len*/) {
	fprintf(stderr, "\x1B[31m" "[X-PAD len]" "\x1B[0m" " ");
}
