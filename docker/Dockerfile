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

# Copy the application script into the container
COPY mqtt_to_pubsub.py .

# The service-account key is NOT baked into the image. Mount it at runtime and
# point Application Default Credentials at it (see docker/README.md).
ENV GOOGLE_APPLICATION_CREDENTIALS=/app/credentials.json

# Run unbuffered so logs stream to `docker logs` in real time.
CMD ["python", "-u", "mqtt_to_pubsub.py"]
