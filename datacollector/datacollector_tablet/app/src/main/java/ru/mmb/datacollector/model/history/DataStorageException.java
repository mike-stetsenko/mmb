package ru.mmb.datacollector.model.history;

public class DataStorageException extends RuntimeException
{
	private static final long serialVersionUID = 6357858314654512639L;

	public DataStorageException(String detailMessage)
	{
		super(detailMessage);
	}
}
