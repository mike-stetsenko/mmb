package ru.mmb.datacollector.activity.input.scanpoint;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemSelectedListener;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.Spinner;

import ru.mmb.datacollector.R;
import ru.mmb.datacollector.activity.ActivityStateWithTeamAndScanPoint;
import ru.mmb.datacollector.activity.StateChangeListener;
import ru.mmb.datacollector.model.ScanPoint;
import ru.mmb.datacollector.model.registry.ScanPointsRegistry;
import ru.mmb.datacollector.model.registry.Settings;

import static ru.mmb.datacollector.activity.Constants.KEY_CURRENT_SCAN_POINT;

public class SelectScanPointActivity extends Activity implements StateChangeListener
{
	private ScanPointsRegistry scanPoints;

	private ActivityStateWithTeamAndScanPoint currentState;

	private Spinner inputScanPoint;
	private Button btnOk;

	private int prevSelectedScanPointPos = -1;
	private int currSelectedScanPointPos = -1;

	@Override
	public void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);

		Settings.getInstance().setCurrentContext(this);

		scanPoints = ScanPointsRegistry.getInstance();

		currentState = new ActivityStateWithTeamAndScanPoint("input.scanpoint");
		currentState.initialize(this, savedInstanceState);

		setContentView(R.layout.input_scanpoint);

		inputScanPoint = (Spinner) findViewById(R.id.inputScanPoint_scanPointInput);
		btnOk = (Button) findViewById(R.id.inputScanPoint_okBtn);

		setInputScanPointAdapter();

		inputScanPoint.setOnItemSelectedListener(new InputScanPointOnItemSelectedListener());
		btnOk.setOnClickListener(new OkBtnClickListener());

		initializeControls();

		currentState.addStateChangeListener(this);
		onStateChange();
	}

	private void setInputScanPointAdapter()
	{
		ArrayAdapter<String> adapter =
				new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, scanPoints.getScanPointNamesArray());
		adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		inputScanPoint.setAdapter(adapter);
	}

	private void initializeControls()
	{
		refreshInputScanPointState();

		if (currentState.getCurrentScanPoint() == null)
		    currentState.setCurrentScanPoint(scanPoints.getScanPointByIndex(0));
		ScanPoint currentScanPoint = currentState.getCurrentScanPoint();
		setInitialScanPointPos(currentScanPoint);
	}

	private void setInitialScanPointPos(ScanPoint currentScanPoint)
	{
		currSelectedScanPointPos = scanPoints.getScanPointIndex(currentScanPoint);
		prevSelectedScanPointPos = currSelectedScanPointPos;
	}

	private void refreshInputScanPointState()
	{
		if (currentState.getCurrentScanPoint() == null)
		{
			inputScanPoint.setSelection(0);
		}
		else
		{
			int pos = scanPoints.getScanPointIndex(currentState.getCurrentScanPoint());
			if (pos == -1) pos = 0;
			inputScanPoint.setSelection(pos);
		}
	}

	private class InputScanPointOnItemSelectedListener implements OnItemSelectedListener
	{
		@Override
		public void onItemSelected(AdapterView<?> parent, View view, int pos, long id)
		{
			currSelectedScanPointPos = pos;
			if (currSelectedScanPointPos != prevSelectedScanPointPos)
			{
				prevSelectedScanPointPos = currSelectedScanPointPos;
				currentState.setCurrentScanPoint(scanPoints.getScanPointByIndex(pos));
			}
		}

		@SuppressWarnings("rawtypes")
		@Override
		public void onNothingSelected(AdapterView parent)
		{
			// Do nothing.
		}
	}

	private class OkBtnClickListener implements OnClickListener
	{
		@Override
		public void onClick(View v)
		{
			Intent resultData = new Intent();
			if (currentState.getCurrentScanPoint() != null)
			    resultData.putExtra(KEY_CURRENT_SCAN_POINT, currentState.getCurrentScanPoint());
			setResult(RESULT_OK, resultData);
			finish();
		}
	}

	@Override
	protected void onSaveInstanceState(Bundle outState)
	{
		super.onSaveInstanceState(outState);
		currentState.save(outState);
	}

	@Override
	public void onStateChange()
	{
		setTitle(currentState.getScanPointText(this));

		btnOk.setEnabled(currentState.isScanPointSelected());
	}
}
