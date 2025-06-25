# Dockerfile

# Use an official Python runtime as a parent image
FROM python:3.9-slim

# Set the working directory in the container
WORKDIR /app

# Copy the requirements file into the container at /app
COPY requirements.txt .

# Install any needed packages specified in requirements.txt
# Use --no-cache-dir to reduce image size
RUN pip install --no-cache-dir -r requirements.txt

# Copy the application script and credentials into the container
COPY mqtt_to_pubsub.py .
COPY credentials.json .

# Run the script when the container launches
CMD ["python", "mqtt_to_pubsub.py"]
