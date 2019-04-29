package ru.mmb.sportiduinomanager;

import android.os.Bundle;
import android.support.v4.content.res.ResourcesCompat;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

import ru.mmb.sportiduinomanager.model.Chips;
import ru.mmb.sportiduinomanager.model.Distance;
import ru.mmb.sportiduinomanager.model.Station;
import ru.mmb.sportiduinomanager.model.Teams;

/**
 * Provides ability to get chip events from station, mark team members as absent,
 * update team members mask in a chip and save this data in local database.
 */
public final class ActivePointActivity extends MainActivity
        implements TeamListAdapter.OnTeamClicked, MemberListAdapter.OnMemberClicked {
    /**
     * Main application thread with persistent data.
     */
    private MainApplication mMainApplication;

    /**
     * Station which was previously paired via Bluetooth.
     */
    private Station mStation;
    /**
     * List of teams and team members from local database.
     */
    private Teams mTeams;
    /**
     * Chips events received from all stations.
     */
    private Chips mChips;
    /**
     * Filtered list of events with teams visiting connected station at current point.
     * One event per team only. Should be equal to records in station flash memory.
     */
    private Chips mFlash;
    /**
     * Local copy of distance (downloaded from site or loaded from local database).
     */
    private Distance mDistance;

    /**
     * User modified team members mask (it can be saved to chip and to local db).
     */
    private int mTeamMask;
    /**
     * Original team mask received from chip at last visit
     * or changed and saved previously.
     */
    private int mOriginalMask;

    /**
     * RecyclerView with team members.
     */
    private MemberListAdapter mMemberAdapter;
    /**
     * RecyclerView with list of teams visited the station.
     */
    private TeamListAdapter mTeamAdapter;

    /**
     * Timer of background thread for communication with the station.
     */
    private Timer mTimer;

    @Override
    protected void onCreate(final Bundle instanceState) {
        super.onCreate(instanceState);
        mMainApplication = (MainApplication) getApplication();
        setContentView(R.layout.activity_activepoint);
        updateMenuItems(mMainApplication, R.id.active_point);
    }

    @Override
    protected void onStart() {
        super.onStart();
        // Set selection in drawer menu to current mode
        getMenuItem(R.id.active_point).setChecked(true);
        // Disable startup animation
        overridePendingTransition(0, 0);

        // Get connected station from main application thread
        mStation = mMainApplication.getStation();
        // Get teams and members from main application thread
        mTeams = mMainApplication.getTeams();
        //Get chips events from main application thread
        mChips = mMainApplication.getChips();
        // Create filtered chip events for current station and point
        if (mChips != null) {
            mFlash = mChips.getChipsAtPoint(mStation.getNumber(), mStation.getMACasLong());
        }
        // Get distance
        mDistance = mMainApplication.getDistance();
        // Initialize masks
        mTeamMask = mMainApplication.getTeamMask();
        mOriginalMask = 0;
        // Prepare recycler view of members list
        final RecyclerView membersList = findViewById(R.id.ap_member_list);
        final RecyclerView.LayoutManager membersLM = new LinearLayoutManager(this);
        membersList.setLayoutManager(membersLM);
        // specify an RecyclerView adapter and initialize it
        mMemberAdapter = new MemberListAdapter(this,
                ResourcesCompat.getColor(getResources(), R.color.text_secondary, getTheme()),
                ResourcesCompat.getColor(getResources(), R.color.bg_secondary, getTheme()));
        membersList.setAdapter(mMemberAdapter);
        // Prepare recycler view of team list
        final RecyclerView teamsList = findViewById(R.id.ap_team_list);
        final RecyclerView.LayoutManager teamsLM = new LinearLayoutManager(this);
        teamsList.setLayoutManager(teamsLM);
        // Specify an RecyclerView adapter and initialize it
        mTeamAdapter = new TeamListAdapter(this, mTeams, mFlash);
        teamsList.setAdapter(mTeamAdapter);

        // Update activity layout
        updateLayout();
        // Start background querying of connected station
        runStationQuerying();
    }

    /**
     * The onClick implementation of TeamListAdapter RecyclerView item click.
     */
    @Override
    public void onTeamClick(final int position) {
        final int oldPosition = mTeamAdapter.getPosition();
        mTeamAdapter.setPosition(position);
        // TODO: save new position in main application
        mTeamAdapter.notifyItemChanged(oldPosition);
        mTeamAdapter.notifyItemChanged(position);
        updateLayout();
    }

    /**
     * The onClick implementation of the RecyclerView team member click.
     */
    @Override
    public void onMemberClick(final int position) {
        // Update team mask
        final int newMask = mTeamMask ^ (1 << position);
        if (newMask == 0) {
            Toast.makeText(mMainApplication,
                    R.string.err_init_empty_team, Toast.LENGTH_LONG).show();
            mMemberAdapter.notifyItemChanged(position);
            return;
        }
        mTeamMask = newMask;
        // Save it to main application
        mMainApplication.setTeamMask(mTeamMask);
        // Update list item
        mMemberAdapter.setMask(mTeamMask);
        mMemberAdapter.notifyItemChanged(position);
        // Display new team members count
        final int teamMembersCount = Teams.getMembersCount(mTeamMask);
        ((TextView) findViewById(R.id.ap_members_count)).setText(getResources()
                .getString(R.string.team_members_count, teamMembersCount));
    }

    /**
     * Save changed team mask to chip amd to local database.
     *
     * @param view View of button clicked
     */
    public void saveTeamMask(final View view) {
        // Check team mask and station presence
        if (mTeamMask == 0 || mStation == null) return;
        // Disable button (it'll be enabled when team mask will change again)
        view.setAlpha(MainApplication.DISABLED_BUTTON);
        view.setClickable(false);
        // TODO: Send command to station
    }

    /**
     * Update layout according to activity state.
     */
    private void updateLayout() {
        // Update number of teams visited this active point
        // (station clock will be updated in background thread)
        ((TextView) findViewById(R.id.ap_total_teams)).setText(getResources()
                .getString(R.string.ap_total_teams, mFlash.size()));
        // Hide last team block when no teams have been visited the station
        if (mFlash == null || mFlash.size() == 0) {
            findViewById(R.id.ap_team_data).setVisibility(View.GONE);
            return;
        }
        // Get index in mFlash team visits list to display
        final int index = mFlash.size() - 1 - mTeamAdapter.getPosition();
        // Update team number and name
        final int teamNumber = mFlash.getTeamNumber(index);
        String teamName;
        if (mTeams == null) {
            teamName = "";
        } else {
            teamName = mTeams.getTeamName(teamNumber);
        }
        ((TextView) findViewById(R.id.ap_team_name)).setText(getResources()
                .getString(R.string.ap_team_name, teamNumber, teamName));
        // Update team time
        ((TextView) findViewById(R.id.ap_team_time)).setText(
                Chips.printTime(mFlash.getTeamTime(index), "dd.MM  HH:mm:ss"));
        // Update lists of visited points
        if (mDistance == null) {
            // TODO: show simplified list of visited points
            findViewById(R.id.ap_visited).setVisibility(View.GONE);
            findViewById(R.id.ap_skipped).setVisibility(View.GONE);
            findViewById(R.id.ap_save_mask).setVisibility(View.GONE);
            return;
        }
        final List<Integer> visited = mChips.getChipMarks(teamNumber, mFlash.getInitTime(index),
                mStation.getNumber(), mStation.getMACasLong(), mDistance.getMaxPoint());
        ((TextView) findViewById(R.id.ap_visited)).setText(getResources()
                .getString(R.string.ap_visited, mDistance.pointsNamesFromList(visited)));
        // Update lists of skipped points
        final List<Integer> skipped = mDistance.getSkippedPoints(visited);
        final TextView skippedText = findViewById(R.id.ap_skipped);
        skippedText.setText(getResources().getString(R.string.ap_skipped,
                mDistance.pointsNamesFromList(skipped)));
        if (mDistance.mandatoryPointSkipped(skipped)) {
            skippedText.setTextColor(ResourcesCompat.getColor(getResources(), R.color.bg_secondary, getTheme()));
        } else {
            skippedText.setTextColor(ResourcesCompat.getColor(getResources(), R.color.text_secondary, getTheme()));
        }
        // Get original mask from last team visit
        mOriginalMask = mFlash.getTeamMask(index);
        if (mOriginalMask <= 0) {
            findViewById(R.id.ap_team_data).setVisibility(View.GONE);
            return;
        }
        // TODO: do it only for new team
        mTeamMask = mOriginalMask;
        mMainApplication.setTeamMask(mTeamMask);
        // Update saveTeamMask button state
        final Button saveMaskButton = findViewById(R.id.ap_save_mask);
        if (mTeamMask == mOriginalMask) {
            saveMaskButton.setAlpha(MainApplication.DISABLED_BUTTON);
            saveMaskButton.setClickable(false);
        } else {
            saveMaskButton.setAlpha(MainApplication.ENABLED_BUTTON);
            saveMaskButton.setClickable(true);
        }
        // Get list of team members
        final List<String> teamMembers = mTeams.getMembersNames(teamNumber);
        // Update list of team members and their selection
        mMemberAdapter.updateList(teamMembers, mOriginalMask, mTeamMask);
        // Update number of team members
        ((TextView) findViewById(R.id.ap_members_count)).setText(getResources()
                .getString(R.string.team_members_count, Teams.getMembersCount(mTeamMask)));
        // Show last team block
        findViewById(R.id.ap_team_data).setVisibility(View.VISIBLE);
    }

    /**
     * Get all events for all new teams
     * which has been visited the station after the last check.
     *
     * @return True if some new teams has been visited the station
     */
    private boolean fetchTeamsVisits() {
        // Do nothing if no teams visited us yet
        if (mStation.getChipsRegistered() == 0) return false;
        // Number of team visits at local db and at station are the same?
        // Time of last visit in local db and at station is the same?
        // (it can change without changing of number of teams)
        if (mFlash.size() == mStation.getChipsRegistered()
                && mFlash.getTeamTime(mFlash.size() - 1) == mStation.getLastChipTime()) {
            return false;
        }
        // Ok, we have some new team visits
        boolean fullDownload = false;
        // Clone previous teams list from station
        final List<Integer> prevLastTeams = new ArrayList<>(mStation.lastTeams());
        // Ask station for new list
        if (!mStation.fetchLastTeams()) {
            fullDownload = true;
        }
        final List<Integer> currLastTeams = mStation.lastTeams();
        // Check if teams from previous list were copied from flash
        for (final int team : prevLastTeams) {
            if (!mFlash.contains(team)) {
                // Something strange has been happened, do full download of all teams
                fullDownload = true;
            }
        }
        // Start building the final list of teams to fetch
        List<Integer> fetchTeams = new ArrayList<>();
        for (final int team : currLastTeams) {
            if (!prevLastTeams.contains(team)) {
                fetchTeams.add(team);
            }
        }
        // If all members of last teams buffer are new to us,
        // then we need to make a full rescan
        if (fetchTeams.size() == Station.LAST_TEAMS_LEN) {
            fullDownload = true;
        }
        // If all last teams are the same but last team time has been changed
        // then we need to rescan all teams from the buffer
        if (fetchTeams.isEmpty()) {
            fetchTeams = currLastTeams;
        }
        // For full rescan of all teams make a list of all registered teams
        if (fullDownload) {
            fetchTeams = mTeams.getTeamList();
        }
        // Get visit parameters for all teams in fetch list
        boolean flashChanged = false;
        boolean newEvents = false;
        for (final int teamNumber : fetchTeams) {
            // Fetch data for the team visit to station
            if (!mStation.fetchTeamRecord(teamNumber)) continue;
            final Chips teamVisit = mStation.getChipEvents();
            if (teamVisit.size() == 0) continue;
            if (teamNumber != teamVisit.getTeamNumber(0)) continue;
            final long initTime = teamVisit.getInitTime(0);
            final int teamMask = teamVisit.getTeamMask(0);
            // Update copy of station flash memory
            if (mFlash.merge(teamVisit)) {
                flashChanged = true;
            }
            // Try to add team visit as new event
            if (mChips.join(teamVisit)) {
                newEvents = true;
                // Read marks from chip and to events list
                final int marks = mStation.getChipRecordsN();
                if (marks == 0) continue;
                int fromMark = 0;
                do {
                    int toRead = marks;
                    if (toRead > Station.MAX_MARK_COUNT) {
                        toRead = Station.MAX_MARK_COUNT;
                    }
                    if (!mStation.fetchTeamMarks(teamNumber, initTime, teamMask, fromMark, toRead)) {
                        break;
                    }
                    fromMark += toRead;
                    // Add fetched chip marks to local list of events
                    mChips.join(mStation.getChipEvents());
                } while (fromMark < marks);
            }
        }
        if (newEvents) {
            // Save new events in local database
            final String result = mChips.saveNewEvents(mMainApplication.getDatabase());
            if (!"".equals(result)) {
                Toast.makeText(getApplicationContext(), result, Toast.LENGTH_LONG).show();
            }
            // Copy changed list of chips events to main application
            mMainApplication.setChips(mChips, false);
        }
        if (flashChanged) {
            // Sort visits by their time
            mFlash.sort();
        }
        return newEvents || flashChanged;
    }

    /**
     * Background thread for periodic querying of connected station.
     */
    private void runStationQuerying() {
        mTimer = new Timer();
        mTimer.scheduleAtFixedRate(new TimerTask() {
            /**
             * Run every 1s, get station status and get new team data (if any).
             */
            public void run() {
                // Station was not connected yet
                if (mStation == null) {
                    stopStationQuerying();
                    return;
                }
                // Fetch current station status
                mStation.fetchStatus();
                // Fetch new new teams visits
                final boolean newTeam = fetchTeamsVisits();
                // Update activity objects
                ActivePointActivity.this.runOnUiThread(() -> {
                    // Update station clock in UI
                    ((TextView) findViewById(R.id.station_clock)).setText(
                            Chips.printTime(mStation.getStationTime(), "dd.MM  HH:mm:ss"));
                    // Display station communication error (if any)
                    if (mStation.getLastError() != 0) {
                        Toast.makeText(getApplicationContext(), mStation.getLastError(),
                                Toast.LENGTH_SHORT).show();
                    }
                    if (newTeam) {
                        // Got new team, update activity layout
                        mTeamAdapter.notifyDataSetChanged();
                        updateLayout();
                    }
                });
            }
        }, 1000, 1000);
    }

    /**
     * Stops rescheduling of periodic station query.
     */
    private void stopStationQuerying() {
        if (mTimer != null) {
            mTimer.cancel();
            mTimer.purge();
        }
    }

    @Override
    protected void onStop() {
        stopStationQuerying();
        super.onStop();
    }
}
